/*
	dpmaster.c

	A master server for DarkPlaces and Q3A, based on Q3A master protocol

	Copyright (C) 2002-2003  Mathieu Olivier

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


#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef WIN32
# include <winsock2.h>
#else
# include <arpa/inet.h>
# include <netdb.h>
# include <netinet/in.h>
# include <pwd.h>
# include <sys/socket.h>
# include <unistd.h>
#endif


// ---------- Constants ---------- //

// Version of dpmaster
#define VERSION "1.3"

// Maximum number of servers in all lists by default
#define DEFAULT_MAX_NB_SERVERS 128

// Address hash: size in bits (between 0 and MAX_HASH_SIZE) and bitmask
#define DEFAULT_HASH_SIZE	5
#define MAX_HASH_SIZE		8
#define HASH_BITMASK		(hash_table_size - 1)

// Number of characters in a challenge, including the '\0'
#define CHALLENGE_LENGTH 12

// Max number of characters for a gamename, including the '\0'
#define GAMENAME_LENGTH 64

// Default master port
#define DEFAULT_MASTER_PORT 27950

// Maximum and minimum sizes for a valid packet
#define MAX_PACKET_SIZE 2048
#define MIN_PACKET_SIZE 5

// Timeouts (in secondes)
#define TIMEOUT_HEARTBEAT		2
#define TIMEOUT_INFORESPONSE	(15 * 60)

// Period of validity for a challenge string (in secondes)
#define VALIDITY_CHALLENGE 2

#ifndef WIN32
// Default path we use for chroot
# define DEFAULT_JAIL_PATH "/var/empty/"

// User we use by default for dropping super-user privileges
# define DEFAULT_LOW_PRIV_USER "nobody"
#endif


// Types of messages (with examples and descriptions):

// The heartbeat is sent by a server when it wants to get noticed by a master.
// A server should send an heartbeat each time its state changes (new map,
// new player, shutdown, ...) and at least each 5 minutes.
// Q3: "heartbeat QuakeArena-1\x0A"
// DP: "heartbeat DarkPlaces\x0A"
// QFusion: "heartbeat QFusion\x0A"
#define S2M_HEARTBEAT "heartbeat"

// "getinfo" is sent by a master to a server when the former needs some infos
// about it. Optionally, a challenge (a string) can be added to the message
// Q3 & DP & QFusion: "getinfo A_Challenge"
#define M2S_GETINFO "getinfo"

// An "infoResponse" message is the reponse to a "getinfo" request
// Its infostring contains a number of important infos about the server.
// "sv_maxclients", "protocol" and "clients" must be present. For DP,
// "gamename" is mandatory too. If the "getinfo" request contained
// a challenge, it must be included (info name: "challenge")
// Q3 & DP & QFusion: "infoResponse\x0A\\pure\\1\\..."
#define S2M_INFORESPONSE "infoResponse\x0A"

/*
Example of packet for "infoReponse" (Q3):

"\xFF\xFF\xFF\xFFinfoResponse\x0A"
"\\sv_allowAnonymous\\0\\pure\\1\\gametype\\0\\sv_maxclients\\8\\clients\\0"
"\\mapname\\q3tourney2\\hostname\\isidore\\protocol\\67\\challenge\\Ch4L-leng3"
*/

// "getservers" is sent by a client who wants to get a list of servers.
// The message must contain a protocol version, and optionally "empty" and/or
// "full" depending on whether or not the client also wants to get empty
// or full servers. DP requires the client to specify the gamemode it runs,
// right before the protocol number.
// Q3: "getservers 67 empty full"
// DP: "getservers DarkPlaces-Quake 3 empty full"
// DP: "getservers DarkPlaces-Hipnotic 3 empty full"
// DP: "getservers DarkPlaces-Rogue 3 empty full"
// DP: "getservers DarkPlaces-Nehahra 3 empty full"
// DP: "getservers Nexuiz 3 empty full"
// DP: "getservers Transfusion 3 empty full"
// QFusion: "getservers qfusion 39 empty full"
#define C2M_GETSERVERS "getservers"

// "getserversResponse" messages contain a list of servers requested
// by a client. It's a list of IPv4 addresses and ports.
// Q3 & DP & QFusion: "getserversResponse\\...(6 bytes)...\\...(6 bytes)...\\EOT\0\0\0"
#define M2C_GETSERVERSREPONSE "getserversResponse"


// ---------- Types ---------- //

// A few basic types
typedef enum {false, true} qboolean;
typedef unsigned char qbyte;

#ifdef WIN32
typedef int socklen_t;
#endif

// Server properties
typedef struct server_s
{
	struct server_s* next;
	struct sockaddr_in address;
	unsigned int protocol;
	qbyte challenge [CHALLENGE_LENGTH];
	unsigned short nbclients;
	unsigned short maxclients;
	time_t timeout;
	time_t challenge_timeout;
	qbyte gamename [GAMENAME_LENGTH];	// DP only
	qboolean active;
} server_t;

// The various messages levels
typedef enum
{
	MSG_NOPRINT,	// used by "max_msg_level" (= no printings)
	MSG_ERROR,		// errors
	MSG_WARNING,	// warnings
	MSG_NORMAL,		// standard messages
	MSG_DEBUG		// for debugging purpose
} msg_level_t;


// ---------- Global variables ---------- //

// All server structures are allocated in one block in the "servers" array.
// Each used slot is also part of a linked list in "hash_table". A simple
// hash of the address and port of a server gives its index in the table.
server_t* servers = NULL;
server_t** hash_table = NULL;
size_t max_nb_servers = DEFAULT_MAX_NB_SERVERS;
size_t hash_table_size = (1 << DEFAULT_HASH_SIZE);
size_t nb_servers = 0;

// Last allocated entry in the "servers" array.
// Used as a start index for finding a free slot in "servers" more quickly
size_t last_alloc;

// Incremented each time we add a server, reset after the call to CheckTimeouts
unsigned int sv_added_count = 0;

// The master socket
int sock = -1;

// The current time (updated every time we receive a packet)
time_t crt_time;

// The port we use
unsigned short master_port = DEFAULT_MASTER_PORT;

// Our own address
struct in_addr localaddr;

// Maximum level for a message to be printed
msg_level_t max_msg_level = MSG_NORMAL;

// Peer address. We rebuild it every time we receive a new packet
char peer_address [128];

#ifndef WIN32
// On UNIX systems, we can run as a daemon
qboolean daemon_mode = false;

// Path we use for chroot
const char* jail_path = DEFAULT_JAIL_PATH;

// Low privileges user
const char* low_priv_user = DEFAULT_LOW_PRIV_USER;
#endif


// ---------- Functions ---------- //

// Win32 uses a different name for some standard functions
#ifdef WIN32
# define snprintf _snprintf
#endif


/*
====================
MsgPrint

Print a message to screen, depending on its verbose level
====================
*/
static int MsgPrint (msg_level_t msg_level, const char* format, ...)
{
	va_list args;
	int result;

	// If the message level is above the maximum level, don't print it
	if (msg_level > max_msg_level)
		return 0;

	va_start (args, format);
	result = vprintf (format, args);
	va_end (args);

	return result;
}


/*
====================
PrintPacket

Print the contents of a packet on stdout
====================
*/
static void PrintPacket (const qbyte* packet, size_t length)
{
	size_t i;

	// Exceptionally, we use MSG_NOPRINT here because if the function is
	// called, the user probably wants this text to be displayed
	// whatever the maximum message level is.
	MsgPrint (MSG_NOPRINT, "\"");

	for (i = 0; i < length; i++)
	{
		qbyte c = packet[i];
		if (c == '\\')
			MsgPrint (MSG_NOPRINT, "\\\\");
		else if (c == '\"')
			MsgPrint (MSG_NOPRINT, "\"");
		else if (c >= 32 && c <= 127)
		 	MsgPrint (MSG_NOPRINT, "%c", c);
		else
			MsgPrint (MSG_NOPRINT, "\\x%02X", c);
	}

	MsgPrint (MSG_NOPRINT, "\" (%u bytes)\n", length);
}

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
			if (c == '\\')
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

				if (c == '\0' || c == '\\')
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
SysInit

System dependent initializations
====================
*/
static qboolean SysInit (void)
{
#ifdef WIN32
	WSADATA winsockdata;

	if (WSAStartup (MAKEWORD (1, 1), &winsockdata))
	{
		MsgPrint (MSG_ERROR, "> ERROR: can't initialize winsocks\n");
		return false;
	}

#else
	// Should we run as a daemon?
	if (daemon_mode && daemon (0, 0))
		MsgPrint (MSG_NOPRINT, "> ERROR: daemonization failed (%s)\n",
				  strerror (errno));

	// UNIX allows us to be completely paranoid, so let's go for it
	if (geteuid () == 0)
	{
		struct passwd* pw;

		MsgPrint (MSG_WARNING, "> WARNING: running with super-user privileges\n");

		// We must get the account infos before the calls to chroot and chdir
		pw = getpwnam (low_priv_user);

		// Chroot ourself
		MsgPrint (MSG_NORMAL, "  - chrooting myself to %s... ", jail_path);
		if (!chroot (jail_path) && !chdir ("/"))
			MsgPrint (MSG_NORMAL, "succeeded\n");
		else
			MsgPrint (MSG_NORMAL, "FAILED (%s)\n", strerror (errno));

		// Switch to lower privileges
		MsgPrint (MSG_NORMAL, "  - switching to user \"%s\" privileges... ",
				  low_priv_user);
		if (pw && !setgid (pw->pw_gid) && !setuid (pw->pw_uid))
			MsgPrint (MSG_NORMAL, "succeeded (UID: %u, GID: %u)\n",
					  pw->pw_uid, pw->pw_gid);
		else
			MsgPrint (MSG_NORMAL, "FAILED (%s)\n", strerror (errno));

		MsgPrint (MSG_NORMAL, "\n");
	}
#endif

	return true;
}


/*
====================
ParseCommandLine

Parse the options passed by the command line
====================
*/
static qboolean ParseCommandLine (int argc, char* argv [])
{
	int ind = 1;
	unsigned int vlevel = max_msg_level;
	qboolean valid_options = true;

	while (ind < argc && valid_options)
	{
		// If it doesn't even look like an option, why bother?
		if (argv[ind][0] != '-')
			valid_options = false;

		else switch (argv[ind][1])
		{
#ifndef WIN32
			// Daemon mode
			case 'D':
				daemon_mode = true;
				break;
#endif

			// Help
			case 'h':
				valid_options = false;
				break;

			// Hash size
			case 'H':
			{
				size_t size;
				ind++;
				if (ind >= argc)
				{
					valid_options = false;
					break;
				}

				size = atoi (argv[ind]);
				if (size <= MAX_HASH_SIZE)
					hash_table_size = 1 << size;
				else
					valid_options = false;
				break;
			}

#ifndef WIN32
			// Jail path
			case 'j':
				ind++;
				if (ind < argc)
					jail_path = argv[ind];
				else
					valid_options = false;
				break;
#endif

			// Maximum number of servers
			case 'n':
			{
				size_t nb = 0;
				ind++;
				if (ind < argc)
					nb = atoi (argv[ind]);
				if (!nb)
					valid_options = false;
				else
					max_nb_servers = nb;
				break;
			}

			// Port number
			case 'p':
			{
				unsigned short port_num = 0;
				ind++;
				if (ind < argc)
					port_num = atoi (argv[ind]);
				if (!port_num)
					valid_options = false;
				else
					master_port = port_num;
				break;
			}

#ifndef WIN32
			// Low privileges user
			case 'u':
				ind++;
				if (ind < argc)
					low_priv_user = argv[ind];
				else
					valid_options = false;
				break;
#endif

			// Verbose level
			case 'v':
				ind++;
				vlevel = ((ind < argc) ? atoi (argv[ind]) : MSG_DEBUG);
				if (vlevel > MSG_DEBUG)
					valid_options = false;
				break;

			default:
				valid_options = false;
		}

		ind++;
	}

	// If the command line is OK, we can set the verbose level now
	if (valid_options)
	{
#ifndef WIN32
		// If we run as a daemon, don't bother printing anything
		if (daemon_mode)
			max_msg_level = MSG_NOPRINT;
		else
#endif
			max_msg_level = vlevel;
	}

	return valid_options;
}


/*
====================
PrintHelp

Print the command line syntax and the available options
====================
*/
static void PrintHelp (void)
{
	MsgPrint (MSG_ERROR,
			  "Syntax: dpmaster [options]\n"
			  "Available options are:\n"
#ifndef WIN32
			  "  -D               : run as a daemon\n"
#endif
			  "  -h               : this help\n"
			  "  -H <hash_size>   : hash size in bits, up to %u (default: %u)\n"
#ifndef WIN32
			  "  -j <jail_path>   : use <jail_path> as chroot path (default: %s)\n"
			  "                     only available when running with super-user privileges\n"
#endif
			  "  -n <max_servers> : maximum number of servers recorded (default: %u)\n"
			  "  -p <port_num>    : use port <port_num> (default: %u)\n"
#ifndef WIN32
			  "  -u <user>        : use <user> privileges (default: %s)\n"
			  "                     only available when running with super-user privileges\n"
#endif
			  "  -v [verbose_lvl] : verbose level, up to %u (default: %u)\n"
			  "\n",
			  MAX_HASH_SIZE, DEFAULT_HASH_SIZE,
#ifndef WIN32
			  DEFAULT_JAIL_PATH,
#endif
			  DEFAULT_MAX_NB_SERVERS,
			  DEFAULT_MASTER_PORT,
#ifndef WIN32
			  DEFAULT_LOW_PRIV_USER,
#endif
			  MSG_DEBUG, MSG_NORMAL);
}


/*
====================
MasterInit

System independent initializations
====================
*/
static qboolean MasterInit (void)
{
	struct sockaddr_in address;
	char localname [256];
	size_t array_size;

	// Init the time and the random seed
	crt_time = time (NULL);
	srand (crt_time);

	// Allocate "servers" and clean it
	array_size = max_nb_servers * sizeof (servers[0]);
	servers = malloc (array_size);
	if (!servers)
	{
		MsgPrint (MSG_ERROR, "> ERROR: can't allocate the servers array (%s)\n",
				  strerror (errno));
		return false;
	}
	last_alloc = max_nb_servers - 1;
	memset (servers, 0, array_size);
	MsgPrint (MSG_NORMAL, "> %u server records allocated\n", max_nb_servers);

	// Allocate "hash_table" and clean it
	array_size = hash_table_size * sizeof (hash_table[0]);
	hash_table = malloc (array_size);
	if (!hash_table)
	{
		MsgPrint (MSG_ERROR, "> ERROR: can't allocate the hash table (%s)\n",
				  strerror (errno));
		free (servers);
		return false;
	}
	memset (hash_table, 0, array_size);
	MsgPrint (MSG_NORMAL, "> Hash table allocated (%u entries)\n", hash_table_size);

	// Get our own non-local address (i.e. not 127.x.x.x)
	localaddr.s_addr = 0;
	if (!gethostname (localname, sizeof (localname)))
	{
		struct hostent *host = gethostbyname (localname);
		if (host && host->h_addrtype == AF_INET && host->h_addr[0] != 127)
		{
			memcpy (&localaddr.s_addr, host->h_addr, sizeof (localaddr.s_addr));
			MsgPrint (MSG_NORMAL,
					  "> Local address resolved: %s => %u.%u.%u.%u\n",
					  localname,
					  host->h_addr[0] & 0xFF, host->h_addr[1] & 0xFF,
					  host->h_addr[2] & 0xFF, host->h_addr[3] & 0xFF);
		}
		else
			MsgPrint (MSG_WARNING,
					  "> WARNING: can't get a non-local IP address for \"%s\"\n",
					  localname);
	}
	else
		MsgPrint (MSG_WARNING, "> WARNING: can't determine the local host name\n");

	// Open the socket
	sock = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
	{
		MsgPrint (MSG_ERROR, "> ERROR: socket creation failed (%s)\n",
				  strerror (errno));
		return false;
	}

	// Bind it to the master port
	memset (&address, 0, sizeof (address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl (INADDR_ANY);
	address.sin_port = htons (master_port);
	if (bind (sock, (struct sockaddr*)&address, sizeof (address)) != 0)
	{
		MsgPrint (MSG_ERROR, "> ERROR: socket binding failed (%s)\n",
				  strerror (errno));
#ifdef WIN32
		closesocket (sock);
#else
		close (sock);
#endif
		return false;
	}
	MsgPrint (MSG_NORMAL, "> Listening on UDP port %hu\n", ntohs (address.sin_port));

	return true;
}


/*
====================
AddressHash

Compute the hash of a server address
====================
*/
static unsigned int AddressHash (const struct sockaddr_in* address)
{
	qbyte* addr = (qbyte*)&address->sin_addr.s_addr;
	qbyte* port = (qbyte*)&address->sin_port;
	qbyte hash;

	hash = addr[0] ^ addr[1] ^ addr[2] ^ addr[3] ^ port[0] ^ port[1];
	return hash & HASH_BITMASK;
}


/*
====================
RemoveServerFromList

Remove a server from its server hash list
====================
*/
static void RemoveServerFromList (server_t* sv, server_t** prev)
{
	nb_servers--;
	MsgPrint (MSG_NORMAL,
			  "> Server %s:%hu has timed out; %u servers are currently registered\n",
			  inet_ntoa (sv->address.sin_addr), ntohs (sv->address.sin_port),
			  nb_servers);

	// Mark this structure as "free"
	sv->active = false;

	*prev = sv->next;
}


/*
====================
GetServer

Search for a particular server in the list; add it if necessary
====================
*/
static server_t* GetServer (const struct sockaddr_in* address, qboolean add_it)
{
	unsigned int i, hash = AddressHash (address);
	server_t **prev, *sv;

	prev = &hash_table[hash];
	sv = hash_table[hash];

	while (sv)
	{
		// We check the timeout values while browsing this list
		if (sv->timeout < crt_time)
		{
			server_t* saved = sv->next;
			RemoveServerFromList (sv, prev);
			sv = saved;
			continue;
		}

		// Found!
		if (sv->address.sin_addr.s_addr == address->sin_addr.s_addr &&
			sv->address.sin_port == address->sin_port)
		{
			// Put it on top of the list (it's useful because heartbeats
			// are almost always followed by infoResponses)
			*prev = sv->next;
			sv->next = hash_table[hash];
			hash_table[hash] = sv;

			return sv;
		}

		prev = &sv->next;
		sv = sv->next;
	}

	if (! add_it)
		return NULL;

	// We increment "sv_added_count" even if there's no free slot left
	// to make sure CheckTimeouts will be called sooner or later
	sv_added_count++;

	// No more room?
	if (nb_servers == max_nb_servers)
		return NULL;

	// Look for the first free entry in "servers"
	for (i = (last_alloc + 1) % max_nb_servers; i != last_alloc; i = (i + 1) % max_nb_servers)
		if (!servers[i].active)
			break;
	sv = &servers[i];
	last_alloc = i;

	// Initialize the structure
	memset (sv, 0, sizeof (*sv));
	memcpy (&sv->address, address, sizeof (sv->address));

	// Add it to the list it belongs to
	sv->next = hash_table[hash];
	hash_table[hash] = sv;
	nb_servers++;

	MsgPrint (MSG_NORMAL,
			  "> New server added: %s; %u servers are currently registered\n",
			  peer_address, nb_servers);
	MsgPrint (MSG_DEBUG, "  - index: %u\n  - hash: 0x%02X\n", i, hash);

	return sv;
}


/*
====================
BuildChallenge

Build a challenge string for a "getinfo" message
====================
*/
static const char* BuildChallenge (void)
{
	static char challenge [CHALLENGE_LENGTH];
	size_t ind;
	size_t length = sizeof (challenge) - 1;  // TODO: make it random?

	for (ind = 0; ind < length; ind++)
	{
		char c;
		do
		{
			c = rand () % (127 - 33) + 33;
		} while (c == '\\' || c == ';' || c == '"' || c == '%');

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
	qbyte msg [64] = "\xFF\xFF\xFF\xFFgetinfo ";

	if (!server->challenge_timeout || server->challenge_timeout < crt_time)
	{
		strcpy (server->challenge, BuildChallenge ());
		server->challenge_timeout = crt_time + VALIDITY_CHALLENGE;
	}

	strcat (msg, server->challenge);
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
static void HandleGetServers (const qbyte* msg, const struct sockaddr_in* addr)
{
	qbyte gamename [GAMENAME_LENGTH] = "";
	qbyte packet [2048] = "\xFF\xFF\xFF\xFFgetserversResponse\\";
	size_t packetind = 23;  // = strlen (packet)
	unsigned int protocol;
	unsigned int ind;
	unsigned int sv_addr;
	unsigned int sv_port;
	qboolean no_empty;
	qboolean no_full;

	MsgPrint (MSG_NORMAL, "> %s ---> getservers\n", peer_address);

	// Check if there's a name before the protocol number
	// In this case, the message comes from a DarkPlaces client
	protocol = atoi (msg);
	if (!protocol)
	{
		char *space;

		strncpy (gamename, msg, sizeof (gamename) - 1);
		gamename[sizeof (gamename) - 1] = '\0';
		space = strchr (gamename, ' ');
		if (space)
			*space = '\0';
		msg += strlen (gamename) + 1;

		protocol = atoi (msg);
	}

	// is the protocol any different for Q2 and QW?

	no_empty = (strstr (msg, "empty") == NULL);
	no_full = (strstr (msg, "full") == NULL);

	MsgPrint (MSG_DEBUG, "> %s <--- getserversResponse\n", peer_address);

	// Add every relevant server
	for (ind = 0; ind < hash_table_size; ind++)
	{
		server_t* sv = hash_table[ind];
		server_t** prev = &hash_table[ind];

		while (sv)
		{
			if (packetind >= sizeof (packet) - 12)
				break;

			// We check the timeout values while browsing the server list
			if (sv->timeout < crt_time)
			{
				server_t* saved = sv->next;
				RemoveServerFromList (sv, prev);
				sv = saved;
				continue;
			}

			// Check protocol, options, and gamename
			if (sv->protocol != protocol ||
				(sv->nbclients == 0 && no_empty) ||
				(sv->nbclients == sv->maxclients && no_full) ||
				(gamename[0] && strcmp (gamename, sv->gamename)))
			{
				// Skip it
				prev = &sv->next;
				sv = sv->next;

				continue;
			}

			sv_addr = ntohl (sv->address.sin_addr.s_addr);
			sv_port = ntohs (sv->address.sin_port);

			// IP address
			packet[packetind    ] =  sv_addr >> 24;
			packet[packetind + 1] = (sv_addr >> 16) & 0xFF;
			packet[packetind + 2] = (sv_addr >>  8) & 0xFF;
			packet[packetind + 3] =  sv_addr        & 0xFF;

			// Port
			packet[packetind + 4] = (sv_port >> 8) & 0xFF;
			packet[packetind + 5] =  sv_port       & 0xFF;

			// Trailing '\'
			packet[packetind + 6] = '\\';

			MsgPrint (MSG_DEBUG, "  - Sending server %u.%u.%u.%u:%u\n",
					  packet[packetind    ], packet[packetind + 1],
					  packet[packetind + 2], packet[packetind + 3],
					  sv_port);

			packetind += 7;
			prev = &sv->next;
			sv = sv->next;
		}
	}

	// End Of Transmission
	packet[packetind    ] = 'E';
	packet[packetind + 1] = 'O';
	packet[packetind + 2] = 'T';
	packet[packetind + 3] = '\0';
	packet[packetind + 4] = '\0';
	packet[packetind + 5] = '\0';
	packetind += 6;

	// Print a few more informations
	MsgPrint (MSG_DEBUG, "  - %u server addresses packed in %u bytes\n",
			  (packetind - 22) / 7 - 1, packetind);

	// Send the packet to the client
	sendto (sock, packet, packetind, 0, (const struct sockaddr*)addr,
			sizeof (*addr));

	// If we have browsed the entire list (and so checked all timeout values),
	// we reset sv_added_count to avoid calling CheckTimeouts anytime soon
	if (ind >= hash_table_size)
		sv_added_count = 0;
}


/*
====================
HandleInfoResponse

Parse infoResponse messages
====================
*/
static void HandleInfoResponse (server_t* server, const qbyte* msg)
{
	char* value;
	unsigned int new_protocol = 0, new_maxclients = 0;

	MsgPrint (MSG_DEBUG, "> %s ---> infoResponse\n", peer_address);

	// Check the challenge
	value = SearchInfostring (msg, "challenge");
	if (!value || strcmp (value, server->challenge))
	{
		MsgPrint (MSG_ERROR, "> ERROR: invalid challenge from %s (%s)\n",
				  peer_address, value);
		return;
	}

	// Check and save the values of "protocol" and "maxclients"
	value = SearchInfostring (msg, "protocol");
	if (value)
		new_protocol = atoi (value);
	value = SearchInfostring (msg, "sv_maxclients");
	if (value)
		new_maxclients = atoi (value);
	if (!new_protocol || !new_maxclients)
	{
		MsgPrint (MSG_ERROR,
				  "> ERROR: invalid data from %s (protocol: %d, maxclients: %d)\n",
				  peer_address, new_protocol, new_maxclients);
		return;
	}
	server->protocol = new_protocol;
	server->maxclients = new_maxclients;

	// Save some other useful values
	value = SearchInfostring (msg, "clients");
	if (value)
		server->nbclients = atoi (value);
	value = SearchInfostring (msg, "gamename");
	if (value)
		strncpy (server->gamename, value, sizeof (server->gamename) - 1);

	// Set a new timeout
	server->timeout = crt_time + TIMEOUT_INFORESPONSE;
}


/*
====================
HandleMessage

Parse a packet to figure out what to do with it
====================
*/
static void HandleMessage (const qbyte* msg, size_t length,
						   const struct sockaddr_in* address)
{
	server_t* server;

	// If it's an heartbeat
	if (!strncmp (S2M_HEARTBEAT, msg, strlen (S2M_HEARTBEAT)))
	{
		char gameId [64];

		// Extract the game id
		sscanf (msg + strlen (S2M_HEARTBEAT) + 1, "%63s", gameId);
		MsgPrint (MSG_DEBUG, "> %s ---> heartbeat (%s)\n", peer_address, gameId);

		// Get the server in the list (add it to the list if necessary)
		server = GetServer (address, true);
		if (server == NULL)
			return;

		server->active = true;

		// If we haven't yet received any infoResponse from this server,
		// we let it some more time to contact us. After that, only
		// infoResponse messages can update the timeout value.
		if (!server->maxclients)
			server->timeout = crt_time + TIMEOUT_HEARTBEAT;

		// Ask for some infos
		SendGetInfo (server);
	}

	// If it's an infoResponse message
	else if (!strncmp (S2M_INFORESPONSE, msg, strlen (S2M_INFORESPONSE)))
	{
		server = GetServer (address, false);
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


/*
====================
CheckTimeouts

Check if some servers have reached their timeout limit
====================
*/
void CheckTimeouts (void)
{
	unsigned int ind;
	server_t* sv;
	server_t** prev;

	// Browse every server list
	for (ind = 0; ind < hash_table_size; ind++)
	{
		sv = hash_table[ind];
		prev = &hash_table[ind];

		while (sv)
		{
			// If the current server has timed out, remove it
			if (sv->timeout < crt_time)
			{
				server_t* saved = sv->next;
				RemoveServerFromList (sv, prev);
				sv = saved;
			}
			else
				sv = sv->next;
		}
	}
}


/*
====================
main

Main function
====================
*/
int main (int argc, char* argv [])
{
	struct sockaddr_in address;
	socklen_t addrlen;
	int nb_bytes;
	qbyte packet [MAX_PACKET_SIZE + 1];  // "+ 1" because we append a '\0'
	qboolean valid_options;

	// Get the options from the command line
	valid_options = ParseCommandLine (argc, argv);

	MsgPrint (MSG_NORMAL,
			  "\n"
			  "dpmaster, a master server for DarkPlaces, Quake III Arena and QFusion\n"
			  "(version " VERSION ", compiled the " __DATE__ " at " __TIME__ ")\n"
			  "\n");

	// If there was a mistake in the command line, print the help and exit
	if (!valid_options)
	{
		PrintHelp ();
		return EXIT_FAILURE;
	}

	// Initializations
	if (!SysInit () || !MasterInit ())
		return EXIT_FAILURE;
	MsgPrint (MSG_NORMAL, "\n");

	// Until the end of times...
	for (;;)
	{
		// Get the next valid message
		addrlen = sizeof (address);
		nb_bytes = recvfrom (sock, packet, sizeof (packet) - 1, 0,
							 (struct sockaddr*)&address, &addrlen);
		if (nb_bytes <= 0)
		{
			MsgPrint (MSG_WARNING, "> WARNING: \"recvfrom\" returned %d\n", nb_bytes);
			continue;
		}

		// We print the packet contents if necessary
		if (max_msg_level >= MSG_DEBUG)
		{
			MsgPrint (MSG_DEBUG, "> New packet received: ");
			PrintPacket (packet, nb_bytes);
		}

		// A few sanity checks
		if (nb_bytes < MIN_PACKET_SIZE)
		{
			MsgPrint (MSG_WARNING, "> WARNING: rejected packet (size = %d bytes)\n",
					  nb_bytes);
			continue;
		}
		if (*((int*)packet) != 0xFFFFFFFF)
		{
			MsgPrint (MSG_WARNING, "> WARNING: rejected packet (invalid header)\n");
			continue;
		}
		if (! ntohs (address.sin_port))
		{
			MsgPrint (MSG_WARNING, "> WARNING: rejected packet (source port = 0)\n");
			continue;
		}

		// If we may have to print something, we rebuild the peer address buffer
		if (max_msg_level != MSG_NOPRINT)
			snprintf (peer_address, sizeof (peer_address), "%s:%hu",
					  inet_ntoa (address.sin_addr), ntohs (address.sin_port));

		// We append a '\0' to make the parsing easier
		packet[nb_bytes] = '\0';

		// We update the current time
		crt_time = time (NULL);

		// If the sender address is the loopback address, we try
		// to translate it into a valid internet address
		if ((ntohl (address.sin_addr.s_addr) >> 24) == 127 && localaddr.s_addr)
			address.sin_addr.s_addr = localaddr.s_addr;

		// Call HandleMessage with the remaining contents
		HandleMessage (packet + 4, nb_bytes - 4, &address);

		// Force a check of all timeouts values every time
		// "sv_added_count" reaches 10% of our storage capability
		if (sv_added_count >= max_nb_servers / 10)
		{
			CheckTimeouts ();
			sv_added_count = 0;
		}
	}
}
