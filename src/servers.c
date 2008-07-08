/*
	servers.c

	Server list and address mapping management for dpmaster

	Copyright (C) 2004-2008  Mathieu Olivier

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include "common.h"
#include "system.h"
#include "servers.h"


// ---------- Constants ---------- //

// Timeout for a newly added server (in secondes)
#define TIMEOUT_HEARTBEAT	2


// ---------- Variables ---------- //

// All server structures are allocated in one block in the "servers" array.
// Each used slot is also part of a linked list in "hash_table". A simple
// hash of the address of a server gives its index in the table.
static server_t* servers = NULL;
static unsigned int max_nb_servers = DEFAULT_MAX_NB_SERVERS;
static unsigned int nb_servers = 0;
static server_t** hash_table_ipv4 = NULL;
static server_t** hash_table_ipv6 = NULL;
static size_t hash_size = DEFAULT_HASH_SIZE;

static unsigned int max_per_address = DEFAULT_MAX_NB_SERVERS_PER_ADDRESS;

// Used to speed up the server allocation / deallocation process
static int last_used_slot = -1;  // -1 = no used slot
static int first_free_slot = 0;  // -1 = no more room

// Variables for Sv_GetFirst, Sv_GetNext and Sv_Remove
static int crt_server_ind = -1;

// List of address mappings. They are sorted by "from" field (IP, then port)
static addrmap_t* addrmaps = NULL;


// ---------- Private functions ---------- //

/*
====================
Sv_AddressHash

Compute the hash of a server address
====================
*/
static unsigned int Sv_AddressHash (const struct sockaddr_storage* address)
{
	unsigned int hash;

	if (address->ss_family == AF_INET6)
	{
		const struct sockaddr_in6* addr6;
		const unsigned int* ipv6_ptr;

		addr6 = (const struct sockaddr_in6*)address;
		ipv6_ptr = (const unsigned int*)&addr6->sin6_addr.s6_addr;
		hash = ipv6_ptr[0] ^ ipv6_ptr[1] ^ ipv6_ptr[2] ^ ipv6_ptr[3];
	}
	else
	{
		const struct sockaddr_in* addr4;
		const unsigned int* ipv4_ptr;

		assert(address->ss_family == AF_INET);

		addr4 = (const struct sockaddr_in*)address;
		ipv4_ptr = (const unsigned int*)&addr4->sin_addr.s_addr;
		hash = ipv4_ptr[0];
	}

	// Merge all the bits in the first 16 bits
	hash = (hash & 0xFFFF) ^ (hash >> 16);

	// Merge the bits to try to not lose too many of them (3 max here)
	if (hash_size >= 8)
	{
		hash = (hash >> hash_size) ^ hash;
	}
	else if (hash_size > 4)
	{
		hash = (hash >> 8) ^ hash;
	}
	else  // if (hash_size <= 4)
	{
		hash = (hash >> 12) ^ (hash >> 8) ^ (hash >>  4) ^ hash;
	}

	return hash & ((1 << hash_size) - 1);
}


/*
====================
Sv_AddToHashTable

Add a server to the hash table
====================
*/
static void Sv_AddToHashTable (server_t* sv, unsigned int hash, server_t** hash_table)
{
	server_t** hash_entry_ptr;

	assert (hash == Sv_AddressHash (&sv->address));

	hash_entry_ptr = &hash_table[hash];
	sv->next = *hash_entry_ptr;
	sv->prev_ptr = hash_entry_ptr;
	*hash_entry_ptr = sv;
	if (sv->next != NULL)
		sv->next->prev_ptr = &sv->next;
}


/*
====================
Sv_RemoveFromHashTable

Remove a server from the hash table
====================
*/
static void Sv_RemoveFromHashTable (server_t* sv)
{
	*sv->prev_ptr = sv->next;
	if (sv->next != NULL)
		sv->next->prev_ptr = sv->prev_ptr;	
}


/*
====================
Sv_Remove

Remove a server from the lists
====================
*/
static void Sv_Remove (server_t* sv)
{
	int sv_ind;

	Sv_RemoveFromHashTable (sv);

	// Mark this structure as "free"
	sv->state = sv_state_unused_slot;

	// Update first_free_slot if necessary
	sv_ind = sv - servers;
	if (first_free_slot == -1 || sv_ind < first_free_slot)
		first_free_slot = sv_ind;

	// If it was the last used slot, look for the previous one
	if (last_used_slot == sv_ind)
		do
		{
			last_used_slot--;
		} while (last_used_slot >= 0 && servers[last_used_slot].state != sv_state_unused_slot);

	nb_servers--;
	MsgPrint (MSG_NORMAL,
	          "> %s timed out; %u server(s) currently registered\n",
	          Sys_SockaddrToString(&sv->address), nb_servers);

	assert (last_used_slot >= (int)nb_servers - 1);
}


/*
====================
Sv_AllocateHashTable

Allocate a hash table
====================
*/
static server_t** Sv_AllocateHashTable (size_t table_size, const char* proto_name)
{
	server_t** result;
	size_t array_size = table_size * sizeof (server_t*);

	result = malloc (array_size);
	if (result != NULL)
	{
		memset (result, 0, array_size);
		MsgPrint (MSG_NORMAL,
				  "> %s hash table allocated (%u entries)\n",
				  proto_name, table_size);
	}
	else
		MsgPrint (MSG_ERROR,
				  "> ERROR: can't allocate the %s hash table (%s)\n",
				  proto_name, strerror (errno));
	
	return result;
}


/*
====================
Sv_IsActive

Return true if a server is active.
Test if the server has timed out and remove it if it's the case.
====================
*/
static qboolean Sv_IsActive (unsigned int sv_ind)
{
	server_t* sv = &servers[sv_ind];

	// If the entry isn't even used
	if (sv->state == sv_state_unused_slot)
		return false;
	
	assert (sv->gamename[0] != '\0' || sv->state == sv_state_uninitialized);

	// If the server has timed out
	if (sv->timeout < crt_time)
	{
		Sv_Remove (sv);
		return false;
	}

	return true;
}


/*
====================
Sv_SameIPv4Addr

Compare 2 IPv4 addresses and return "true" if they're equal
====================
*/
static qboolean Sv_SameIPv4Addr (const struct sockaddr_storage* addr1,
								 const struct sockaddr_storage* addr2,
								 qboolean* same_ip_address)
{
	const struct sockaddr_in *addr1_in, *addr2_in;

	addr1_in = (const struct sockaddr_in*)addr1;
	addr2_in = (const struct sockaddr_in*)addr2;

	// Same address?
	if (addr1_in->sin_addr.s_addr == addr2_in->sin_addr.s_addr)
	{
		*same_ip_address = true;

		// Same port?
		if (addr1_in->sin_port == addr2_in->sin_port)
			return true;
	}
	else
		*same_ip_address = false;

	return false;
}


/*
====================
Sv_SameIPv6Addr

Compare 2 IPv6 addresses and return "true" if they're equal
====================
*/
static qboolean Sv_SameIPv6Addr (const struct sockaddr_storage* addr1,
								 const struct sockaddr_storage* addr2,
								 qboolean* same_ip_address)
{
	const struct sockaddr_in6 *addr1_in6, *addr2_in6;

	addr1_in6 = (const struct sockaddr_in6*)addr1;
	addr2_in6 = (const struct sockaddr_in6*)addr2;

	// Same address?
	if (memcmp (&addr1_in6->sin6_addr.s6_addr, &addr2_in6->sin6_addr.s6_addr,
				sizeof(addr1_in6->sin6_addr.s6_addr)) == 0)
	{
		*same_ip_address = true;

		// Same port?
		if (addr1_in6->sin6_port == addr2_in6->sin6_port)
			return true;
	}
	else
		*same_ip_address = false;

	return false;
}


/*
====================
Sv_GetByAddr_Internal

Search for a particular server in the list
====================
*/
static server_t* Sv_GetByAddr_Internal (const struct sockaddr_storage* address, unsigned int* same_address_found)
{
	unsigned int hash = Sv_AddressHash (address);
	server_t** hash_table;
	server_t* sv;
	qboolean (*IsSameAddress) (const struct sockaddr_storage* addr1, const struct sockaddr_storage* addr2, qboolean* same_address);
	
	if (address->ss_family == AF_INET6)
	{
		hash_table = hash_table_ipv6;
		IsSameAddress = Sv_SameIPv6Addr;
	}
	else
	{
		assert (address->ss_family == AF_INET);
		hash_table = hash_table_ipv4;
		IsSameAddress = Sv_SameIPv4Addr;
	}
	sv = hash_table[hash];

	*same_address_found = 0;
	while (sv != NULL)
	{
		server_t* next_sv = sv->next;

		if (Sv_IsActive(sv - servers))
		{
			// Same address?
			qboolean same_ip_address;
			qboolean same_address;

			same_ip_address = false;
			same_address = IsSameAddress (&sv->address, address, &same_ip_address);
			if (same_ip_address)
				*same_address_found += 1;
			if (same_address)
			{
				// Move it on top of the list (it's useful because heartbeats
				// are almost always followed by infoResponses)
				Sv_RemoveFromHashTable (sv);
				Sv_AddToHashTable (sv, hash, hash_table);

				return sv;
			}
		}
		
		sv = next_sv;
	}

	return NULL;
}


/*
====================
Sv_CheckTimeouts

Browse the server list and remove all the servers that have timed out
====================
*/
static void Sv_CheckTimeouts (void)
{
	int ind;
	
	for (ind = 0; ind <= last_used_slot; ind++)
		Sv_IsActive (ind);
}


/*
====================
Sv_ResolveAddr

Resolve an internet address
name may include a port number, after a ':'
====================
*/
static qboolean Sv_ResolveAddr (const char* name, struct sockaddr_in* addr)
{
	char *namecpy, *port;
	struct hostent* host;

	// Create a work copy
	namecpy = strdup (name);
	if (namecpy == NULL)
	{
		MsgPrint (MSG_ERROR,
				  "> ERROR: can't allocate enough memory to resolve %s\n",
				  name);
		return false;
	}

	// Find the port in the address
	port = strchr (namecpy, ':');
	if (port != NULL)
		*port++ = '\0';

	// Resolve the address
	host = gethostbyname (namecpy);
	if (host == NULL)
	{
		MsgPrint (MSG_ERROR, "> ERROR: can't resolve %s\n", namecpy);
		free (namecpy);
		return false;
	}
	if (host->h_addrtype != AF_INET)
	{
		MsgPrint (MSG_ERROR, "> ERROR: %s is not an IPv4 address\n",
				  namecpy);
		free (namecpy);
		return false;
	}

	// Build the structure
	memset (addr, 0, sizeof (*addr));
	addr->sin_family = AF_INET;
	memcpy (&addr->sin_addr.s_addr, host->h_addr,
			sizeof (addr->sin_addr.s_addr));
	if (port != NULL)
	{
		char* end_ptr;
		unsigned short port_num;
		
		port_num = (unsigned short)strtol (port, &end_ptr, 0);
		if (end_ptr == port || *end_ptr != '\0' || port_num == 0)
		{
			MsgPrint (MSG_ERROR, "> ERROR: %s is not a valid port number\n",
					  port);
			free (namecpy);
			return false;
		}
		addr->sin_port = htons (port_num);
	}

	MsgPrint (MSG_DEBUG, "> \"%s\" resolved to %s:%hu\n",
			  name, inet_ntoa (addr->sin_addr), ntohs (addr->sin_port));

	free (namecpy);
	return true;
}


/*
====================
Sv_InsertAddrmapIntoList

Insert an addrmap structure to the addrmaps list
====================
*/
static void Sv_InsertAddrmapIntoList (addrmap_t* new_map)
{
	addrmap_t* addrmap = addrmaps;
	addrmap_t** prev = &addrmaps;
	char from_addr [16];

	// Stop at the end of the list, or if the addresses become too high
	while (addrmap != NULL &&
		   addrmap->from.sin_addr.s_addr <= new_map->from.sin_addr.s_addr)
	{
		// If we found the right place
		if (addrmap->from.sin_addr.s_addr == new_map->from.sin_addr.s_addr &&
			addrmap->from.sin_port >= new_map->from.sin_port)
		{
			// If a mapping is already recorded for this address
			if (addrmap->from.sin_port == new_map->from.sin_port)
			{
				MsgPrint (MSG_WARNING,
						  "> WARNING: overwritting the previous mapping of address %s:%hu\n",
						  inet_ntoa (new_map->from.sin_addr),
						  ntohs (new_map->from.sin_port));

				*prev = addrmap->next;
				free (addrmap->from_string);
				free (addrmap->to_string);
				free (addrmap);
			}
			break;
		}

		prev = &addrmap->next;
		addrmap = addrmap->next;
	}

	// Insert it
	new_map->next = *prev;
	*prev = new_map;

	strncpy (from_addr, inet_ntoa (new_map->from.sin_addr), sizeof(from_addr) - 1);
	from_addr[sizeof(from_addr) - 1] = '\0';
	MsgPrint (MSG_NORMAL, "> Address \"%s\" (%s:%hu) mapped to \"%s\" (%s:%hu)\n",
			  new_map->from_string, from_addr, ntohs (new_map->from.sin_port),
			  new_map->to_string, inet_ntoa (new_map->to.sin_addr), ntohs (new_map->to.sin_port));
}


/*
====================
Sv_GetAddrmap

Look for an address mapping corresponding to addr
====================
*/
static const addrmap_t* Sv_GetAddrmap (const struct sockaddr_in* addr)
{
	const addrmap_t* addrmap = addrmaps;
	const addrmap_t* found = NULL;

	// Stop at the end of the list, or if the addresses become too high
	while (addrmap != NULL &&
		   addrmap->from.sin_addr.s_addr <= addr->sin_addr.s_addr)
	{
		// If it's the right address
		if (addrmap->from.sin_addr.s_addr == addr->sin_addr.s_addr)
		{
			// If the exact mapping isn't there
			if (addrmap->from.sin_port > addr->sin_port)
				return found;

			// If we found the exact address
			if (addrmap->from.sin_port == addr->sin_port)
				return addrmap;

			// General mapping
			// Store it in case we don't find the exact address mapping
			if (addrmap->from.sin_port == 0)
				found = addrmap;
		}

		addrmap = addrmap->next;
	}

	return found;
}


/*
====================
Sv_ResolveAddrmap

Resolve an addrmap structure and check the parameters validity
====================
*/
static qboolean Sv_ResolveAddrmap (addrmap_t* addrmap)
{
	// Resolve the addresses
	if (!Sv_ResolveAddr (addrmap->from_string, &addrmap->from) ||
		!Sv_ResolveAddr (addrmap->to_string, &addrmap->to))
		return false;

	// 0.0.0.0 addresses are forbidden
	if (addrmap->from.sin_addr.s_addr == 0 ||
		addrmap->to.sin_addr.s_addr == 0)
	{
		MsgPrint (MSG_ERROR,
				  "> ERROR: Mapping from or to 0.0.0.0 is forbidden\n");
		return false;
	}

	// Do NOT allow mapping to loopback addresses
	if ((ntohl (addrmap->to.sin_addr.s_addr) >> 24) == 127)
	{
		MsgPrint (MSG_ERROR,
				  "> ERROR: Mapping to a loopback address is forbidden\n");
		return false;
	}

	return true;
}


// ---------- Public functions (servers) ---------- //

/*
====================
Sv_SetHashSize

Set a new hash size value
====================
*/
qboolean Sv_SetHashSize (unsigned int size)
{
	// Too late? Too small or too big?
	if (hash_table_ipv4 != NULL || hash_table_ipv6 != NULL ||
		size < 0 || size > MAX_HASH_SIZE)
		return false;

	hash_size = size;
	return true;
}


/*
====================
Sv_SetMaxNbServers

Set a new maximum number of servers
====================
*/
qboolean Sv_SetMaxNbServers (unsigned int nb)
{
	// Too late? Or too small?
	if (servers != NULL || nb <= 0)
		return false;

	max_nb_servers = nb;
	return true;
}


/*
====================
Sv_SetMaxNbServersPerAddress

Set a new maximum number of servers for one given IP address
====================
*/
qboolean Sv_SetMaxNbServersPerAddress (unsigned int nb)
{
	// Too late?
	if (servers != NULL)
		return false;

	max_per_address = nb;
	return true;
}


/*
====================
Sv_Init

Initialize the server list and hash table
====================
*/
qboolean Sv_Init (void)
{
	unsigned int hash_table_size;
	size_t array_size;

	// Allocate "servers" and clean it
	array_size = max_nb_servers * sizeof (servers[0]);
	servers = malloc (array_size);
	if (!servers)
	{
		MsgPrint (MSG_ERROR,
				  "> ERROR: can't allocate the servers array (%s)\n",
				  strerror (errno));
		return false;
	}
	memset (servers, 0, array_size);
	MsgPrint (MSG_NORMAL, "> %u server records allocated (maximum number per address: ", max_nb_servers);
	if (max_per_address == 0)
		MsgPrint (MSG_NORMAL, "unlimited)\n");
	else
		MsgPrint (MSG_NORMAL, "%u)\n", max_per_address);

	// Allocate the hash tables and clean them
	hash_table_size = (1 << hash_size);
	if (Sys_IsListeningOn (AF_INET))
	{
		hash_table_ipv4 = Sv_AllocateHashTable (hash_table_size, "IPv4");
		if (hash_table_ipv4 == NULL)
			return false;
	}
	if (Sys_IsListeningOn (AF_INET6))
	{
		hash_table_ipv6 = Sv_AllocateHashTable (hash_table_size, "IPv6");
		if (hash_table_ipv6 == NULL)
			return false;
	}

	return true;
}


/*
====================
Sv_GetByAddr

Search for a particular server in the list; add it if necessary
====================
*/
server_t* Sv_GetByAddr (const struct sockaddr_storage* address, socklen_t addrlen, qboolean add_it)
{
	unsigned int nb_same_address = 0;
	server_t *sv;
	const addrmap_t* addrmap;
	unsigned int hash;
	unsigned int ind;
	server_t** hash_table;

	sv = Sv_GetByAddr_Internal (address, &nb_same_address);
	if (sv != NULL)
	{
		assert (addrlen == sv->addrlen);
		return sv;
	}

	if (! add_it)
		return NULL;

	assert (nb_same_address <= max_per_address || max_per_address == 0);
	if (nb_same_address >= max_per_address && max_per_address != 0)
	{
		MsgPrint (MSG_WARNING,
				  "> WARNING: server %s isn't allowed (max number of servers reached for this address)\n",
				  peer_address);
		return NULL;
	}

	// Allow servers on a loopback address ONLY if a mapping is defined for them
	if (address->ss_family == AF_INET)
	{
		const struct sockaddr_in* addr_in = (const struct sockaddr_in*)address;
		addrmap = Sv_GetAddrmap (addr_in);
		if ((ntohl (addr_in->sin_addr.s_addr) >> 24) == 127 && addrmap == NULL)
		{
			MsgPrint (MSG_WARNING,
					  "> WARNING: server %s isn't allowed (loopback address without address mapping)\n",
					  peer_address);
			return NULL;
		}
	}
	else
	{
		const struct sockaddr_in6 *addr_in6 = (const struct sockaddr_in6*)address;
		if (memcmp (&addr_in6->sin6_addr.s6_addr, &in6addr_loopback.s6_addr,
					sizeof(addr_in6->sin6_addr.s6_addr)) == 0)
		{
			MsgPrint (MSG_WARNING,
					  "> WARNING: server %s isn't allowed (IPv6 loopback address)\n",
					  peer_address);
			return NULL;
		}

		addrmap = NULL;
	}

	// If the list is full, check the entries to see if we can free a slot
	if (nb_servers == max_nb_servers)
	{
		assert (last_used_slot == (int)max_nb_servers - 1);
		assert (first_free_slot == -1);

		Sv_CheckTimeouts ();
		if (nb_servers == max_nb_servers)
			return NULL;
	}

	// Use the first free entry in "servers"
	assert (first_free_slot != -1);
	assert (-1 <= last_used_slot);
	assert (last_used_slot < (int)max_nb_servers);
	sv = &servers[first_free_slot];
	if (last_used_slot < first_free_slot)
		last_used_slot = first_free_slot;

	// Look for the next free entry in "servers"
	ind = (unsigned int)first_free_slot + 1;
	first_free_slot = -1;
	while (ind < max_nb_servers)
	{
		if (! Sv_IsActive(ind))
		{
			first_free_slot = (int)ind;
			break;
		}

		ind++;
	}

	// Initialize the structure
	memset (sv, 0, sizeof (*sv));
	memcpy (&sv->address, address, sizeof (sv->address));
	sv->addrlen = addrlen;
	sv->addrmap = addrmap;

	// Add it to the list it belongs to
	hash = Sv_AddressHash (address);
	if (address->ss_family == AF_INET6)
		hash_table = hash_table_ipv6;
	else
		hash_table = hash_table_ipv4;
	Sv_AddToHashTable (sv, hash, hash_table);

	sv->state = sv_state_uninitialized;
	sv->timeout = crt_time + TIMEOUT_HEARTBEAT;

	nb_servers++;

	MsgPrint (MSG_NORMAL,
			  "> New server added: %s. %u server(s) now registered, including %u at this IP address\n",
			  peer_address, nb_servers, nb_same_address + 1);
	MsgPrint (MSG_DEBUG,
			  "  - index: %u\n"
			  "  - hash: 0x%04X\n",
			  (unsigned int)(sv - servers), hash);

	return sv;
}


/*
====================
Sv_GetFirst

Get the first server in the list
====================
*/
server_t* Sv_GetFirst (void)
{
	crt_server_ind = -1;

	return Sv_GetNext ();
}


/*
====================
Sv_GetNext

Get the next server in the list
====================
*/
server_t* Sv_GetNext (void)
{
	assert(last_used_slot >= -1);
	assert(last_used_slot < (int)max_nb_servers);

	while (crt_server_ind < last_used_slot)
	{
		crt_server_ind++;
		if (Sv_IsActive(crt_server_ind))
			return &servers[crt_server_ind];
	}

	return NULL;
}


/*
====================
Sv_PrintServerList

Print the list of servers to the output
====================
*/
void Sv_PrintServerList (msg_level_t msg_level)
{
	int ind;

	MsgPrint (msg_level, "\n> %u servers registered (time: %lu):\n",
			  nb_servers, (unsigned long)crt_time);

	for (ind = 0; ind <= last_used_slot; ind++)
		if (Sv_IsActive(ind))
		{
			const server_t* sv = &servers[ind];
			const char* state_string;

			MsgPrint (msg_level, " * %s", Sys_SockaddrToString (&sv->address));
			if (sv->addrmap != NULL)
				MsgPrint (msg_level, ", mapped to %s", sv->addrmap->to_string);

			assert(sv->state > sv_state_unused_slot);
			assert(sv->state <= sv_state_full);
			switch (sv->state)
			{
				case sv_state_unused_slot:
					state_string = "unused";
					break;
				case sv_state_uninitialized:
					state_string = "not initialized";
					break;
				case sv_state_empty:
					state_string = "empty";
					break;
				case sv_state_occupied:
					state_string = "occupied";
					break;
				case sv_state_full:
					state_string = "full";
					break;
				default:
					state_string = "UNKNOWN";
					break;
			}

			MsgPrint (msg_level,
					  " (timeout: %lu)\n"
					  "\tgame: \"%s\" (protocol: %d)\n"
					  "\tstate: %s\n"
					  "\tchallenge: \"%s\" (timeout: %lu)\n",
					  (unsigned long)sv->timeout,
					  sv->gamename, sv->protocol,
					  state_string,
					  sv->challenge, (unsigned long)sv->challenge_timeout);
		}
}


// ---------- Public functions (address mappings) ---------- //

/*
====================
Sv_AddAddressMapping

Add an unresolved address mapping to the list
mapping must be of the form "addr1:port1=addr2:port2", ":portX" are optional
====================
*/
qboolean Sv_AddAddressMapping (const char* mapping)
{
	char *map_string, *to_ip; 
	addrmap_t* addrmap;

	// Get a working copy of the mapping string
	map_string = strdup (mapping);
	if (map_string == NULL)
	{
		MsgPrint (MSG_ERROR,
				  "> ERROR: can't allocate address mapping string\n");
		return false;
	}

	// Find the '='
	to_ip = strchr (map_string, '=');
	if (to_ip == NULL)
	{
		MsgPrint (MSG_ERROR,
				  "> ERROR: invalid syntax in address mapping string\n");
		free (map_string);
		return false;
	}
	*to_ip++ = '\0';

	// Allocate the structure
	addrmap = malloc (sizeof (*addrmap));
	if (addrmap == NULL)
	{
		MsgPrint (MSG_ERROR,
				  "> ERROR: can't allocate address mapping structure\n");
		free (map_string);
		return false;
	}
	memset (addrmap, 0, sizeof (*addrmap));

	addrmap->from_string = strdup (map_string);
	addrmap->to_string = strdup (to_ip);
	free (map_string);
	if (addrmap->from_string == NULL || addrmap->to_string == NULL)
	{
		MsgPrint (MSG_ERROR,
				  "> ERROR: can't allocate address mapping strings\n");
		free (addrmap->to_string);
		free (addrmap->from_string);
		return false;
	}

	// Add it on top of "addrmaps"
	addrmap->next = addrmaps;
	addrmaps = addrmap;

	return true;
}


/*
====================
Sv_ResolveAddressMappings

Resolve the address mapping list
====================
*/
qboolean Sv_ResolveAddressMappings (void)
{
	addrmap_t* addrmap;

	// Resolve all addresses
	for (addrmap = addrmaps; addrmap != NULL; addrmap = addrmap->next)
		if (!Sv_ResolveAddrmap (addrmap))
			return false;
	
	// Sort the list
	addrmap = addrmaps;
	addrmaps = NULL;
	while (addrmap != NULL)
	{
		addrmap_t* next_addrmap = addrmap->next;
		
		Sv_InsertAddrmapIntoList (addrmap);
		addrmap = next_addrmap;
	}

	return true;
}
