/*
	messages.c

	Message management for dpmaster

	Copyright (C) 2004-2006  Mathieu Olivier

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
#include "messages.h"
#include "servers.h"


// ---------- Constants ---------- //

// Timeout after a valid infoResponse (in secondes)
#define TIMEOUT_INFORESPONSE (15 * 60)

// Period of validity for a challenge string (in secondes)
#define TIMEOUT_CHALLENGE 2

// Gamename used for Q3A
#define GAMENAME_Q3A "Quake3Arena"

// Maximum size of a reponse packet
#define MAX_PACKET_SIZE 1400


// Types of messages (with samples):

// Q3: "heartbeat QuakeArena-1\x0A"
// DP: "heartbeat DarkPlaces\x0A"
#define S2M_HEARTBEAT "heartbeat"

// Q3 & DP & QFusion: "getinfo A_Challenge"
#define M2S_GETINFO "getinfo"

// Q3 & DP & QFusion: "infoResponse\x0A\\pure\\1\\..."
#define S2M_INFORESPONSE "infoResponse\x0A"

// Q3: "getservers 67 empty full"
// DP: "getservers DarkPlaces-Quake 3 empty full"
// DP: "getservers Transfusion 3 empty full"
// QFusion: "getservers qfusion 39 empty full"
#define C2M_GETSERVERS "getservers "

// Q3 & DP & QFusion:
// "getserversResponse\\...(6 bytes)...\\...(6 bytes)...\\EOT\0\0\0"
#define M2C_GETSERVERSREPONSE "getserversResponse"



// ---------- Private functions ---------- //

/*
====================
SearchInfostring

Search an infostring for the value of a key
====================
*/
static char* SearchInfostring (const char* infostring, const char* key)
{
	static char value [256];
	char crt_key [256];
	size_t value_ind, key_ind;
	char c;

	if (*infostring++ != '\\')
		return NULL;

	value_ind = 0;
	for (;;)
	{
		key_ind = 0;

		// Get the key name
		for (;;)
		{
			c = *infostring++;

			if (c == '\0')
				return NULL;
			if (c == '\\' || key_ind == sizeof (crt_key) - 1)
			{
				crt_key[key_ind] = '\0';
				break;
			}

			crt_key[key_ind++] = c;
		}

		// If it's the key we are looking for, save it in "value"
		if (!strcmp (crt_key, key))
		{
			for (;;)
			{
				c = *infostring++;

				if (c == '\0' || c == '\\' || value_ind == sizeof (value) - 1)
				{
					value[value_ind] = '\0';
					return value;
				}

				value[value_ind++] = c;
			}
		}

		// Else, skip the value
		for (;;)
		{
			c = *infostring++;

			if (c == '\0')
				return NULL;
			if (c == '\\')
				break;
		}
	}
}


/*
====================
BuildChallenge

Build a challenge string for a "getinfo" message
====================
*/
static const char* BuildChallenge (void)
{
	static char challenge [CHALLENGE_MAX_LENGTH];
	size_t ind;
	size_t length = CHALLENGE_MIN_LENGTH - 1;  // We start at the minimum size

	// ... then we add a random number of characters
	length += rand () % (CHALLENGE_MAX_LENGTH - CHALLENGE_MIN_LENGTH + 1);

	for (ind = 0; ind < length; ind++)
	{
		char c;
		do
		{
			c = 33 + rand () % (126 - 33 + 1);  // -> c = 33..126
		} while (c == '\\' || c == ';' || c == '"' || c == '%' || c == '/');

		challenge[ind] = c;
	}

	challenge[length] = '\0';
	return challenge;
}


/*
====================
SendGetInfo

Send a "getinfo" message to a server
====================
*/
static void SendGetInfo (server_t* server)
{
	char msg [64] = "\xFF\xFF\xFF\xFF" M2S_GETINFO " ";
	size_t msglen;

	if (!server->challenge_timeout || server->challenge_timeout < crt_time)
	{
		const char* challenge;

		challenge = BuildChallenge ();
		strncpy (server->challenge, challenge, sizeof (server->challenge) - 1);
		server->challenge_timeout = crt_time + TIMEOUT_CHALLENGE;
	}

	msglen = strlen (msg);
	strncpy (msg + msglen, server->challenge, sizeof (msg) - msglen - 1);
	msg[sizeof (msg) - 1] = '\0';
	sendto (sock, msg, strlen (msg), 0,
			(const struct sockaddr*)&server->address,
			sizeof (server->address));

	MsgPrint (MSG_DEBUG, "> %s <--- getinfo with challenge \"%s\"\n",
			  peer_address, server->challenge);
}


/*
====================
HandleGetServers

Parse getservers requests and send the appropriate response
====================
*/
static void HandleGetServers (const char* msg, const struct sockaddr_in* addr)
{
	const char* packetheader = "\xFF\xFF\xFF\xFF" M2C_GETSERVERSREPONSE "\\";
	const size_t headersize = strlen (packetheader);
	char* end_ptr;
	const char* msg_ptr;
	char gamename [GAMENAME_LENGTH] = "";
	qbyte packet [MAX_PACKET_SIZE];
	size_t packetind;
	server_t* sv;
	int protocol;
	qboolean no_empty;
	qboolean no_full;

	// Check if there's a name before the protocol number
	// In this case, the message comes from a DarkPlaces-compatible client
	protocol = (int)strtol (msg, &end_ptr, 0);
	if (end_ptr == msg || (*end_ptr != ' ' && *end_ptr != '\0'))
	{
		char *space;

		strncpy (gamename, msg, sizeof (gamename) - 1);
		gamename[sizeof (gamename) - 1] = '\0';
		space = strchr (gamename, ' ');
		if (space)
			*space = '\0';
		msg_ptr = msg + strlen (gamename) + 1;

		protocol = atoi (msg_ptr);
	}
	// Else, it comes from a Quake III Arena client
	else
	{
		strncpy (gamename, GAMENAME_Q3A, sizeof (gamename) - 1);
		gamename[sizeof (gamename) - 1] = '\0';
		msg_ptr = msg;
	}

	MsgPrint (MSG_NORMAL, "> %s ---> getservers (%s)\n", peer_address,
			  gamename);

	no_empty = (strstr (msg_ptr, "empty") == NULL);
	no_full = (strstr (msg_ptr, "full") == NULL);

	// Initialize the packet contents with the header
	packetind = headersize;
	memcpy(packet, packetheader, headersize);

	// Add every relevant server
	for (sv = Sv_GetFirst (); /* see below */;  sv = Sv_GetNext ())
	{
		unsigned int sv_addr;
		unsigned short sv_port;

		// If we're done, or if the packet is full, send the packet
		if (sv == NULL || packetind > sizeof (packet) - (7 + 6))
		{
			// End Of Transmission
			packet[packetind    ] = 'E';
			packet[packetind + 1] = 'O';
			packet[packetind + 2] = 'T';
			packet[packetind + 3] = '\0';
			packet[packetind + 4] = '\0';
			packet[packetind + 5] = '\0';
			packetind += 6;

			// Send the packet to the client
			sendto (sock, packet, packetind, 0, (const struct sockaddr*)addr,
					sizeof (*addr));

			MsgPrint (MSG_DEBUG, "> %s <--- getserversResponse (%u servers)\n",
						peer_address, (packetind - headersize - 6) / 7);

			// If we're done
			if (sv == NULL)
				return;
			
			// Reset the packet index (no need to change the header)
			packetind = headersize;
		}

		assert (sv->state != sv_state_unused_slot);

		sv_addr = ntohl (sv->address.sin_addr.s_addr);
		sv_port = ntohs (sv->address.sin_port);

		// Extra debugging info
		if (max_msg_level >= MSG_DEBUG)
		{
			MsgPrint (MSG_DEBUG,
					  "Comparing server: IP:\"%u.%u.%u.%u:%hu\", p:%d, g:\"%s\"\n",
					  sv_addr >> 24, (sv_addr >> 16) & 0xFF,
					  (sv_addr >>  8) & 0xFF, sv_addr & 0xFF,
					  sv_port, sv->protocol, sv->gamename);

			if (sv->protocol != protocol)
				MsgPrint (MSG_DEBUG,
						  "Reject: protocol %d != requested %d\n",
						  sv->protocol, protocol);
			if (sv->state <= sv_state_uninitialized)
				MsgPrint (MSG_DEBUG, "Reject: server is not initialized\n");
			if (sv->state == sv_state_empty && no_empty)
				MsgPrint (MSG_DEBUG, "Reject: server is empty && no_empty\n");
			if (sv->state == sv_state_full && no_full)
				MsgPrint (MSG_DEBUG, "Reject: server is full && no_full\n");
			if (strcmp (gamename, sv->gamename) != 0)
				MsgPrint (MSG_DEBUG,
						  "Reject: gamename \"%s\" != requested \"%s\"\n",
						  sv->gamename, gamename);
		}

		// Check protocol, options, and gamename
		if (sv->state <= sv_state_uninitialized ||
			sv->protocol != protocol ||
			(sv->state == sv_state_empty && no_empty) ||
			(sv->state == sv_state_full && no_full) ||
			strcmp (gamename, sv->gamename) != 0)
		{
			// Skip it
			continue;
		}

		// Use the address mapping associated with the server, if any
		if (sv->addrmap != NULL)
		{
			const addrmap_t* addrmap = sv->addrmap;

			sv_addr = ntohl (addrmap->to.sin_addr.s_addr);
			if (addrmap->to.sin_port != 0)
				sv_port = ntohs (addrmap->to.sin_port);

			MsgPrint (MSG_DEBUG,
					  "Server address mapped to %u.%u.%u.%u:%hu\n",
					  sv_addr >> 24, (sv_addr >> 16) & 0xFF,
					  (sv_addr >>  8) & 0xFF, sv_addr & 0xFF,
					  sv_port);
		}

		// IP address
		packet[packetind    ] =  sv_addr >> 24;
		packet[packetind + 1] = (sv_addr >> 16) & 0xFF;
		packet[packetind + 2] = (sv_addr >>  8) & 0xFF;
		packet[packetind + 3] =  sv_addr        & 0xFF;

		// Port
		packet[packetind + 4] = sv_port >> 8;
		packet[packetind + 5] = sv_port & 0xFF;

		// Trailing '\'
		packet[packetind + 6] = '\\';

		MsgPrint (MSG_DEBUG, "  - Sending server %u.%u.%u.%u:%hu\n",
				  packet[packetind    ], packet[packetind + 1],
				  packet[packetind + 2], packet[packetind + 3],
				  sv_port);

		packetind += 7;
	}
}


/*
====================
HandleInfoResponse

Parse infoResponse messages
====================
*/
static void HandleInfoResponse (server_t* server, const char* msg)
{
	char* value;
	int new_protocol;
	char* end_ptr;
	unsigned int new_maxclients, new_clients;

	MsgPrint (MSG_DEBUG, "> %s ---> infoResponse\n", peer_address);

	// Check the challenge
	if (!server->challenge_timeout || server->challenge_timeout < crt_time)
	{
		MsgPrint (MSG_WARNING,
				  "> WARNING: infoResponse with obsolete challenge from %s\n",
				  peer_address);
		return;
	}
	value = SearchInfostring (msg, "challenge");
	if (!value || strcmp (value, server->challenge))
	{
		MsgPrint (MSG_ERROR, "> ERROR: invalid challenge from %s (%s)\n",
				  peer_address, value);
		return;
	}

	// Check the value of "protocol"
 	value = SearchInfostring (msg, "protocol");
	if (value == NULL)
	{
		MsgPrint (MSG_ERROR,
				  "> ERROR: invalid infoResponse from %s (no protocol value)\n",
				  peer_address);
		return;
	}
	new_protocol = (int)strtol (value, &end_ptr, 0);
	if (end_ptr == value || *end_ptr != '\0')
	{
		MsgPrint (MSG_ERROR,
				  "> ERROR: invalid infoResponse from %s (invalid protocol value: %s)\n",
				  peer_address, value);
		return;
	}

	// Check the value of "maxclients"
	value = SearchInfostring (msg, "sv_maxclients");
	new_maxclients = ((value != NULL) ? atoi (value) : 0);
	if (new_maxclients == 0)
	{
		MsgPrint (MSG_ERROR,
				  "> ERROR: invalid infoResponse from %s (sv_maxclients = %d)\n",
				  peer_address, new_maxclients);
		return;
	}

	// Check the presence of "clients"
	value = SearchInfostring (msg, "clients");
	if (value == NULL)
	{
		MsgPrint (MSG_ERROR,
				  "> ERROR: invalid infoResponse from %s (no \"clients\" value)\n",
				  peer_address);
		return;
	}
	new_clients = ((value != NULL) ? atoi (value) : 0);

	// Q3A doesn't send a gamename, so we add it manually
	value = SearchInfostring (msg, "gamename");
	if (value == NULL)
		value = GAMENAME_Q3A;
	else if (value[0] == '\0')
	{
		MsgPrint (MSG_ERROR,
				  "> ERROR: invalid infoResponse from %s (game name is void)\n",
				  peer_address);
		return;
	}

	// If the gamename has changed
	if (strcmp (server->gamename, value) != 0)
	{
		// If the server had already been initialized, warn about it
		if (server->gamename[0] != '\0')
		{
			assert (server->state > sv_state_uninitialized);
			MsgPrint (MSG_WARNING,
					  "> Server %s updated its gamename: \"%s\" -> \"%s\"\n",
					  peer_address, server->gamename, value);
		}
		else
			assert (server->state == sv_state_uninitialized);

		strncpy (server->gamename, value, sizeof (server->gamename) - 1);
	}

	// Save some useful informations in the server entry
	server->protocol = new_protocol;
	if (new_clients == 0)
		server->state = sv_state_empty;
	else if (new_clients == new_maxclients)
		server->state = sv_state_full;
	else
		server->state = sv_state_occupied;

	// Set a new timeout
	server->timeout = crt_time + TIMEOUT_INFORESPONSE;
}


// ---------- Public functions ---------- //

/*
====================
HandleMessage

Parse a packet to figure out what to do with it
====================
*/
void HandleMessage (const char* msg, size_t length,
					const struct sockaddr_in* address)
{
	server_t* server;

	// If it's an heartbeat
	if (!strncmp (S2M_HEARTBEAT, msg, strlen (S2M_HEARTBEAT)))
	{
		char gameId [64];

		// Extract the game id
		sscanf (msg + strlen (S2M_HEARTBEAT) + 1, "%63s", gameId);
		MsgPrint (MSG_DEBUG, "> %s ---> heartbeat (%s)\n",
				  peer_address, gameId);

		// Get the server in the list (add it to the list if necessary)
		server = Sv_GetByAddr (address, true);
		if (server == NULL)
			return;

		assert (server->state != sv_state_unused_slot);

		// Ask for some infos
		SendGetInfo (server);
	}

	// If it's an infoResponse message
	else if (!strncmp (S2M_INFORESPONSE, msg, strlen (S2M_INFORESPONSE)))
	{
		server = Sv_GetByAddr (address, false);
		if (server == NULL)
			return;

		HandleInfoResponse (server, msg + strlen (S2M_INFORESPONSE));
	}

	// If it's a getservers request
	else if (!strncmp (C2M_GETSERVERS, msg, strlen (C2M_GETSERVERS)))
	{
		HandleGetServers (msg + strlen (C2M_GETSERVERS), address);
	}
}
