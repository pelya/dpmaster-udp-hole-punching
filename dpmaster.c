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
# include <netinet/in.h>
# include <sys/socket.h>
# include <unistd.h>
#endif


// ------ Constants ------ //

// Maximum number of servers in the list
#define MAX_NB_SERVERS 512

// Number of characters in a challenge
#define CHALLENGE_LENGTH 11

// Number of characters for a gamename
#define GAMENAME_LENGTH 63

// Default master port
#define DEFAULT_MASTER_PORT 27950

// Maximum and minimum sizes for a valid packet
#define MAX_PACKET_SIZE 2048
#define MIN_PACKET_SIZE 5

// Timeouts (in secondes)
#define TIMEOUT_HEARTBEAT    2
#define TIMEOUT_INFORESPONSE (15 * 60)


// Types of messages (with examples and description):

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


// ------ Types ------ //

// A few basic types
typedef enum {false, true} qboolean;
typedef unsigned char byte;

#ifdef WIN32
typedef int socklen_t;
#endif

// Supported games
typedef enum {GAME_QUAKE3, GAME_DARKPLACES} game_t;

// Server properties
typedef struct
{
	struct sockaddr_in address;
	unsigned int protocol;
	byte challenge [CHALLENGE_LENGTH + 1];
	unsigned short nbclients;
	unsigned short maxclients;
	time_t timeout;
	game_t game;
	byte gamename [GAMENAME_LENGTH + 1];  // DP only
} server_t;


// ------ Global variables ------ //

// The server list is sorted from the smallest IP address to the highest one
// We make sure there's no hole into the list; the first "nbservers" slots
// are used, and the others are not.
// FIXME: well, it's not sorted yet...  :oP
server_t servers [MAX_NB_SERVERS];
size_t nbservers = 0;

// LordHavoc: print extra info about what is happening
int testmode = 0;

// The master socket
int sock;


// ------ Functions ------ //

/*
====================
PrintPacket

Print the contents of a packet on stdout
====================
*
static void PrintPacket (const byte* packet, size_t length)
{
	size_t i;
	for (i = 0; i < length; i++)
	{
		byte c = packet[i];
		if (c == '\\')
			printf ("\\\\");
		else if (c >= 32 && c <= 127)
			putchar (c);
		else
			printf ("\\x%02X", c);
	}
}
*/

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
MasterInit

System independent initializations
====================
*/
static qboolean MasterInit (int argc, char* argv [])
{
	struct sockaddr_in address;

	// Init the random seed
	srand (time (NULL));

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
	address.sin_port = htons (DEFAULT_MASTER_PORT);
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

	printf ("* Listening on port %hu\n\n", ntohs (address.sin_port));
	return true;
}


/*
====================
GetServer

Search for a particular server in the list; add it if necessary
====================
*/
static server_t* GetServer (const struct sockaddr_in* address, qboolean add_it)
{
	size_t ind;
	server_t* sv;

	for (ind = 0; ind < nbservers; ind++)
	{
		sv = &servers[ind];
		if (sv->address.sin_addr.s_addr == address->sin_addr.s_addr &&
			sv->address.sin_port == address->sin_port)
			return sv;
	}

	if (!add_it)
		return NULL;

	// No more room?
	if (nbservers == sizeof (servers) / sizeof (servers[0]))
		return NULL;

	sv = &servers[nbservers];
	memset (sv, 0, sizeof (*sv));
	memcpy (&sv->address, address, sizeof (*address));
	sv->timeout = time (NULL) + 3;

	printf ("  - Added to the server list (number %u)\n", nbservers);
	nbservers++;

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
	static char challenge [CHALLENGE_LENGTH + 1];
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
	byte msg [64] = "\xFF\xFF\xFF\xFFgetinfo ";
	const char* challenge = BuildChallenge ();

	strcat (msg, challenge);
	sendto (sock, msg, strlen (msg), 0,
			(const struct sockaddr*)&server->address,
			sizeof (server->address));

	strcpy (server->challenge, challenge);

	printf ("* %s:%hu <--- getinfo with challenge \"%s\"\n",
			inet_ntoa (server->address.sin_addr),
			ntohs (server->address.sin_port), challenge);
}


/*
====================
HandleGetServers

Parse getservers requests and send the appropriate response
====================
*/
static void HandleGetServers (const byte* msg, const struct sockaddr_in* addr)
{
	byte gamename [GAMENAME_LENGTH + 1] = "";
	byte packet [2048] = "\xFF\xFF\xFF\xFFgetserversResponse\\";
	size_t packetind = 23;  // = strlen (packet)
	unsigned int protocol;
	qboolean no_empty;
	qboolean no_full;
	unsigned int ind;
	unsigned int sv_addr;
	unsigned int sv_port;
	game_t game;
	time_t crttime = time (NULL);

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

	// Add every relevant server
	for (ind = 0; ind < nbservers; ind++)
	{
		server_t* sv = &servers[ind];

		if (packetind >= sizeof (packet) - 12)
			break;

		if (sv->game != game)
			continue;
		if (sv->protocol != protocol)
			continue;
		if (sv->nbclients == 0 && no_empty)
			continue;
		if (sv->nbclients == sv->maxclients && no_full)
			continue;
		if (gamename[0] && strcmp (gamename, sv->gamename))
			continue;

		// The removal of old servers is done after the parsing,
		// so we also need to check the timeout value here
		if (sv->timeout < crttime)
			continue;

		sv_addr = ntohl (sv->address.sin_addr.s_addr);
		sv_port = ntohs (sv->address.sin_port);
		if (testmode)
			printf("sending server %d.%d.%d.%d:%d\n", (sv_addr >> 24) & 0xFF, (sv_addr >> 16) & 0xFF, (sv_addr >> 8) & 0xFF, sv_addr & 0xFF, sv_port);

		// IP address
		packet[packetind    ] =  sv_addr >> 24;
		packet[packetind + 1] = (sv_addr >> 16) & 0xFF;
		packet[packetind + 2] = (sv_addr >>  8) & 0xFF;
		packet[packetind + 3] =  sv_addr        & 0xFF;
		packetind += 4;

		// Port
		packet[packetind    ] = (sv_port >> 8) & 0xFF;
		packet[packetind + 1] =  sv_port       & 0xFF;
		packetind += 2;

		// Trailing '\'
		packet[packetind++] = '\\';
	}

	// End Of Transmission
	packet[packetind    ] = 'E';
	packet[packetind + 1] = 'O';
	packet[packetind + 2] = 'T';
	packet[packetind + 3] = '\0';
	packet[packetind + 4] = '\0';
	packet[packetind + 5] = '\0';
	packetind += 6;

	// Print the packet contents on stdout
	printf ("* %s:%hu <--- getserversResponse (%u bytes => %u servers)\n",
			inet_ntoa (addr->sin_addr), ntohs (addr->sin_port),
			packetind, (packetind - 22) / 7 - 1);

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
static void HandleInfoResponse (server_t* server, const byte* msg)
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
	server->timeout = time (NULL) + TIMEOUT_INFORESPONSE;
}


/*
====================
HandleMessage

Parse a packet to figure out what to do with it
====================
*/
static void HandleMessage (const byte* msg, size_t length,
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
		server->timeout = time (NULL) + TIMEOUT_HEARTBEAT;

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
	time_t crttime = time (NULL);
	unsigned int nbremoved = 0;

	while (ind < nbservers)
	{
		// If the current server hasn't timed out, go check the next one
		if (servers[ind].timeout >= crttime)
		{
			ind++;
			continue;
		}

		if (ind < nbservers - 1)
			memmove (&servers[ind], &servers[ind + 1],
						(nbservers - ind - 1)  * sizeof (servers[ind]));

		printf ("* Server %u timed out\n", ind + nbremoved);
		nbservers--;
		nbremoved++;
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
	byte packet [MAX_PACKET_SIZE + 1];  // "+ 1" because we append a '\0'

	printf ("\n"
			"dpmaster, a master server for DarkPlaces and Q3A\n"
			"(compiled the " __DATE__ " at " __TIME__ ")\n"
			"\n");

	// Initializations
	if (!SysInit ())
		return EXIT_FAILURE;
	if (!MasterInit (argc, argv))
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
			printf ("* ERROR: Recvfrom returned %d\n", nb_bytes);
			continue;
		}

		// We append a '\0' to make the parsing easier
		packet[nb_bytes] = '\0';

		if (testmode)
			PrintPacket (packet, nb_bytes);
		// TODO: if the address is the loopback address, we should translate
		//       it into a valid internet address

		// Call HandleMessage with the remaining content
		HandleMessage (packet + 4, nb_bytes - 4, &address);

		// Check if some servers have reached their timeout limit
		CheckTimeouts ();
	}

	// We can't be there anyhow
	return EXIT_SUCCESS;
}
