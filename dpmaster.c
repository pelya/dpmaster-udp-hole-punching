/*
	dpmaster.c

	A master server for DarkPlaces based on Q3A master protocol

	Copyright (C) 2002  Mathieu Olivier

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


#include <assert.h>
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
# include <sys/socket.h>
# include <unistd.h>
#endif


// ---------- Constants ---------- //

// Version of dpmaster
#define VERSION "1.0"

// Maximum number of servers in all lists by default
#define DEFAULT_MAX_NB_SERVERS 256

// Size of an address hash, in bits
#define HASH_SIZE 8

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
#define TIMEOUT_CHALLENGE    2
#define TIMEOUT_HEARTBEAT    2
#define TIMEOUT_INFORESPONSE (15 * 60)


// Types of messages (with examples and descriptions):

// The heartbeat is sent by a server when it wants to get noticed by a master.
// A server should send an heartbeat each time its state changes (new map,
// new player, shutdown, ...) and at least each 5 minutes.
// Q3: "heartbeat QuakeArena-1\x0A"
// DP: "heartbeat DarkPlaces\x0A"
#define S2M_HEARTBEAT "heartbeat"

// "getinfo" is sent by a master to a server when the former needs some infos
// about it. Optionally, a challenge (a string) can be added to the message
// Q3 & DP: "getinfo A_Challenge"
#define M2S_GETINFO "getinfo"

// An "infoResponse" message is the reponse to a "getinfo" request
// Its infostring contains a number of important infos about the server.
// "sv_maxclients", "protocol" and "clients" must be present. For DP, "gamename"
// is mandatory too. If the "getinfo" request contained a challenge,
// it must be included (info name: "challenge")
// Q3 & DP: "infoResponse\x0A\\pure\\1\\..."
#define S2M_INFORESPONSE "infoResponse"

/*
Example of packet for "infoReponse" (Q3):

"\xFF\xFF\xFF\xFFinfoResponse\x0A"
"\\sv_allowAnonymous\\0\\pure\\1\\gametype\\0\\sv_maxclients\\8\\clients\\0"
"\\mapname\\q3tourney2\\hostname\\isidore\\protocol\\67\\challenge\\Ch4L-leng3"
*/

// "getservers" is sent by a client who wants to get a list of servers.
// The message must contain a protocol version, and optionally "empty" and/or
// "full" depending on whether or not the client also wants to get full servers
// or empty servers. DP requires the client to precise the gamemode it runs,
// right before the protocol number.
// Q3: "getservers 67 empty full"
// DP: "getservers Transfusion 3 empty full"
#define C2M_GETSERVERS "getservers"

// "getserversResponse" messages contains a list of servers requested
// by a client. It's a list of IPv4 addresses and ports.
// Q3 & DP: "getserversResponse\\...(6 bytes)...\\...(6 bytes)...\\EOT\0\0\0"
#define M2C_GETSERVERSREPONSE "getserversResponse"


// ---------- Types ---------- //

// A few basic types
typedef enum {false, true} qboolean;
typedef unsigned char qbyte;

#ifdef WIN32
typedef int socklen_t;
#endif

// Supported games
typedef enum {GAME_QUAKE3, GAME_DARKPLACES} game_t;

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
	time_t challenge_timeout;  // each challenge remains valid during a short time
	game_t game;
	qbyte gamename [GAMENAME_LENGTH];  // DP only
} server_t;


// ---------- Global variables ---------- //

// All server structures are allocated in one block in the "servers" array.
// Each used slot is also part of a linked list in "server_lists". A simple
// hash of the address and port of the server gives the index of the list
// used for a particular server.
server_t* servers;
server_t* server_lists [1 << HASH_SIZE];
size_t max_nb_servers = DEFAULT_MAX_NB_SERVERS;
size_t nb_servers = 0;

// Last allocated entry in the "servers" array.
// Used as a start index for finding a free slot in "servers" more quickly
size_t last_alloc;

// Print extra info about what is happening
qboolean testmode = false;

// The master socket
int sock;

// The current time (updated every time we receive a packet)
time_t crt_time;

// The port we use
unsigned short master_port = DEFAULT_MASTER_PORT;

// Our own address
struct in_addr localaddr;


// ---------- Functions ---------- //

/*
====================
PrintPacket

Print the contents of a packet on stdout
====================
*/
static void PrintPacket (const qbyte* packet, size_t length)
{
	size_t i;

	putchar ('"');

	for (i = 0; i < length; i++)
	{
		qbyte c = packet[i];
		if (c == '\\')
			printf ("\\\\");
		else if (c >= 32 && c <= 127)
			putchar (c);
		else
			printf ("\\x%02X", c);
	}

	printf ("\" (%u bytes)\n", length);
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
		printf ("* ERROR: can't initialize winsocks\n");
		return false;
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
	qboolean print_help = false;

	while (ind < argc && !print_help)
	{
		// If it doesn't even look like an option, why bother?
		if (argv[ind][0] != '-')
			print_help = true;

		else switch (argv[ind][1])
		{
			// Port number
			case 'p':
			{
				unsigned short port_num = 0;
				ind++;
				if (ind < argc)
					port_num = atoi (argv[ind]);
				if (!port_num)
					print_help = true;
				else
					master_port = port_num;
				break;
			}

			// Maximum number of servers
			case 'n':
			{
				size_t nb = 0;
				ind++;
				if (ind < argc)
					nb = atoi (argv[ind]);
				if (!nb)
					print_help = true;
				else
					max_nb_servers = nb;
				break;
			}

			// Test mode
			case 't':
				testmode = true;
				break;

			default:
				print_help = true;
		}
		
		ind++;
	}
	
	if (print_help)
	{
		printf ("Syntax: dpmaster [options]\n"
				"Options can be:\n"
				"	-n <max_servers> : maximum number of servers recorded (default: %u)\n"
				"	-p <port_num>    : listen on port <port_num> (default: %hu)\n"
				"	-t               : test mode (print more informations, for debugging)\n",
				DEFAULT_MAX_NB_SERVERS, DEFAULT_MASTER_PORT);

		return false;
	}

	return true;
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

	// Allocate "servers" and clean the arrays
	array_size = max_nb_servers * sizeof (servers[0]);
	servers = malloc (array_size);
	if (!servers)
	{
		perror ("Can't allocate the servers array");
		return false;
	}
	last_alloc = max_nb_servers - 1;
	memset (servers, 0, array_size);
	memset (server_lists, 0, sizeof (server_lists));
	printf ("* %u server records allocated\n", max_nb_servers);
	
	// Get our own internet address (i.e. not 127.x.x.x)
	localaddr.s_addr = 0;
	if (!gethostname (localname, sizeof (localname)))
	{
		struct hostent *host = gethostbyname (localname);
		if (host && host->h_addrtype == AF_INET && host->h_addr[0] != 127)
		{
			memcpy (&localaddr.s_addr, host->h_addr, sizeof (localaddr.s_addr));
			printf ("* Local address resolved (%s => %u.%u.%u.%u)\n", localname,
					host->h_addr[0] & 0xFF, host->h_addr[1] & 0xFF,
					host->h_addr[2] & 0xFF, host->h_addr[3] & 0xFF);
		}
		else
			printf ("* WARNING: can't determine the local host address\n");
	}
	else
		printf ("* WARNING: can't determine the local host name\n");

	// Open the socket
	sock = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
	{
		perror ("Socket failed");
		return false;
	}

	// Bind it to the master port
    memset (&address, 0, sizeof (address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl (INADDR_ANY);
	address.sin_port = htons (master_port);
	if (bind (sock, (struct sockaddr*)&address, sizeof (address)) != 0)
	{
		perror ("Bind failed");
#ifdef WIN32
		closesocket (sock);
#else
		close (sock);
#endif
		return false;
	}
	printf ("* Listening on port %hu\n", ntohs (address.sin_port));

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
	return (addr[0] ^ addr[1] ^ addr[2] ^ addr[3] ^ port[0] ^ port[1]);
}


/*
====================
GetServer

Search for a particular server in the list; add it if necessary
====================
*/
static server_t* GetServer (const struct sockaddr_in* address, qboolean add_it)
{
	unsigned int hash = AddressHash (address);
	server_t *prev = NULL, *sv = server_lists[hash];
	unsigned int i;

	while (sv)
	{
		if (sv->address.sin_addr.s_addr == address->sin_addr.s_addr &&
			sv->address.sin_port == address->sin_port)
		{
			// Put it on top of the list
			// (useful because heartbeats are always followed by infoResponses)
			if (prev)
			{
				prev->next = sv->next;
				sv->next = server_lists[hash];
				server_lists[hash] = sv;
			}

			return sv;
		}

		prev = sv;
		sv = sv->next;
	}
	
	if (!add_it)
		return NULL;

	// No more room?
	if (nb_servers == max_nb_servers)
		return NULL;

	// Look for the first free entry in "servers"
	for (i = (last_alloc + 1) % max_nb_servers; i != last_alloc; i = (i + 1) % max_nb_servers)
		if (! servers[i].address.sin_port)
			break;
	sv = &servers[i];
	last_alloc = i;

	// Initialize the structure
	memset (sv, 0, sizeof (*sv));
	memcpy (&sv->address, address, sizeof (*address));
	sv->timeout = crt_time + 3;

	// Add it to the list it belongs
	sv->next = server_lists[hash];
	server_lists[hash] = sv;
	nb_servers++;

	printf ("  - Added to the server list (hash: 0x%02X)\n", hash);

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
		server->challenge_timeout = crt_time + TIMEOUT_CHALLENGE;
	}

	strcat (msg, server->challenge);
	sendto (sock, msg, strlen (msg), 0,
			(const struct sockaddr*)&server->address,
			sizeof (server->address));

	printf ("* %s:%hu <--- getinfo with challenge \"%s\"\n",
			inet_ntoa (server->address.sin_addr),
			ntohs (server->address.sin_port), server->challenge);
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
	qboolean no_empty;
	qboolean no_full;
	unsigned int ind;
	unsigned int sv_addr;
	unsigned int sv_port;
	game_t game;

	printf ("* %s:%hu ---> getservers\n",
			inet_ntoa (addr->sin_addr), ntohs (addr->sin_port));

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
		game = GAME_DARKPLACES;
	}
	else
		game = GAME_QUAKE3;

	no_empty = (strstr (msg, "empty") == NULL);
	no_full = (strstr (msg, "full") == NULL);

	printf ("* %s:%hu <--- getserversResponse\n",
			inet_ntoa (addr->sin_addr), ntohs (addr->sin_port));

	// Add every relevant server
	for (ind = 0; ind < nb_servers; ind++)
	{
		server_t* sv = &servers[ind];

		if (packetind >= sizeof (packet) - 12)
			break;

		if (sv->game != game)  // same game?
			continue;
		if (sv->protocol != protocol)  // same protocol?
			continue;
		if (sv->nbclients == 0 && no_empty)  // send empty servers?
			continue;
		if (sv->nbclients == sv->maxclients && no_full)  // send full servers?
			continue;
		if (gamename[0] && strcmp (gamename, sv->gamename))  // same mod?
			continue;

		// The removal of old servers is done after the parsing,
		// so we also need to check the timeout value here
		if (sv->timeout < crt_time)
			continue;

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

		if (testmode)
			printf ("  - Sending server %u.%u.%u.%u:%u\n",
					packet[packetind    ], packet[packetind + 1],
					packet[packetind + 2], packet[packetind + 3],
					sv_port);

		packetind += 7;
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
	printf ("  - %u servers in %u bytes\n",
			(packetind - 22) / 7 - 1, packetind);

	// Send the packet to the client
	sendto (sock, packet, packetind, 0, (const struct sockaddr*)addr,
			sizeof (*addr));
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

	printf ("* %s:%hu ---> infoResponse\n",
			inet_ntoa (server->address.sin_addr),
			ntohs (server->address.sin_port));

	// Check the challenge
	value = SearchInfostring (msg + 13, "challenge");
	if (!value || strcmp (value, server->challenge))
	{
		printf ("  - ERROR: invalid challenge (%s)\n", value);
		// FIXME: remove it from the list?
		return;
	}
		printf ("  - Valid challenge\n");

	// Save some useful values
	value = SearchInfostring (msg + 13, "protocol");
	if (value)
		server->protocol = atoi (value);
	value = SearchInfostring (msg + 13, "sv_maxclients");
	if (value)
		server->maxclients = atoi (value);
	value = SearchInfostring (msg + 13, "clients");
	if (value)
		server->nbclients = atoi (value);
	value = SearchInfostring (msg + 13, "gamename");
	if (value)
		strncpy (server->gamename, value, sizeof (server->gamename) - 1);
	if (!server->protocol || !server->maxclients)
	{
		printf ("  - ERROR: invalid data (protocol: %d, maxclients: %d)\n",
				server->protocol, server->maxclients);
		// FIXME: remove it from the list?
		return;
	}

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
		game_t game;

		printf ("* %s:%hu ---> heartbeat\n",
				inet_ntoa (address->sin_addr), ntohs (address->sin_port));

		printf ("  - Game: ");
		if (!strcmp (msg + 9, " DarkPlaces\x0A"))
		{
			game = GAME_DARKPLACES;
			printf ("DarkPlaces\n");
		}
		else if (!strcmp (msg + 9, " QuakeArena-1\x0A"))
		{
			game = GAME_QUAKE3;
			printf ("Quake3Arena\n");
		}
		else
		{
			printf ("UNKNOWN\n");
			return;
		}

		// Get the server in the list (add it to the list if necessary)
		server = GetServer (address, true);
		if (server == NULL)
			return;

		server->game = game;

		// Set the new timeout value
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

		HandleInfoResponse (server, msg);
	}

	// If it's a getservers request
	else if (!strncmp (C2M_GETSERVERS, msg, strlen (C2M_GETSERVERS)))
	{
		HandleGetServers (msg + 11, address);
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
	unsigned int ind = 0;
	server_t *prev, *sv;

	// Browse every server list
	for (ind = 0; ind < sizeof (server_lists) / sizeof (server_lists[0]); ind++)
	{
		for (prev = NULL, sv = server_lists[ind]; sv != NULL; sv = sv->next)
		{
			// If the current server has timed out, remove it
			if (sv->timeout < crt_time)
			{
				if (prev)
					prev->next = sv->next;
				else
					server_lists[ind] = sv->next;

				nb_servers--;
				printf ("* Server %s:%hu has timed out (%u servers remaining)\n",
						inet_ntoa (sv->address.sin_addr),
						ntohs (sv->address.sin_port), nb_servers);
				
				// Mark it as "free"
				sv->address.sin_port = 0;
			}
			else
				prev = sv;
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

	printf ("\n"
			"dpmaster, a master server for DarkPlaces and Q3A\n"
			"(version " VERSION ", compiled the " __DATE__ " at " __TIME__ ")\n"
			"\n");

	// Initializations
	if (!ParseCommandLine (argc, argv) || !SysInit () || !MasterInit ())
		return EXIT_FAILURE;

	// Until the end of times...
	for (;;)
	{
		// Get the next valid message
		addrlen = sizeof (address);
		nb_bytes = recvfrom (sock, packet, sizeof (packet) - 1, 0,
							 (struct sockaddr*)&address, &addrlen);
		if (nb_bytes <= MIN_PACKET_SIZE || *((int*)packet) != 0xFFFFFFFF)
		{
			printf ("* WARNING: \"recvfrom\" returned %d\n", nb_bytes);
			continue;
		}

		// We append a '\0' to make the parsing easier
		packet[nb_bytes] = '\0';

		// We update the current time and print the packet if necessary
		crt_time = time (NULL);
		if (testmode)
			PrintPacket (packet, nb_bytes);

		// If the sender address is the loopback address, we try
		// to translate it into a valid internet address
		if ((ntohl (address.sin_addr.s_addr) >> 24) == 127 && localaddr.s_addr)
			address.sin_addr.s_addr = localaddr.s_addr;

		// Call HandleMessage with the remaining content
		HandleMessage (packet + 4, nb_bytes - 4, &address);

		// Check if some servers have reached their timeout limit
		CheckTimeouts ();
	}
}
