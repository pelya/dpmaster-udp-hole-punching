/*
	messages.c

	Message management for dpmaster

	Copyright (C) 2004  Mathieu Olivier

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

// Timeouts (in secondes)
#define TIMEOUT_HEARTBEAT		2
#define TIMEOUT_INFORESPONSE	(15 * 60)

// Period of validity for a challenge string (in secondes)
#define TIMEOUT_CHALLENGE 2

// Gamename used for Q3A
#define GAMENAME_Q3A "Quake3Arena"


// Types of messages (with samples):

// Q3: "heartbeat QuakeArena-1\x0A"
// DP: "heartbeat DarkPlaces\x0A"
// QFusion: "heartbeat QFusion\x0A"
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
	qbyte msg [64] = "\xFF\xFF\xFF\xFF" M2S_GETINFO " ";

	if (!server->challenge_timeout || server->challenge_timeout < crt_time)
	{
		strncpy (server->challenge, BuildChallenge (),
				 sizeof (server->challenge) - 1);
		server->challenge_timeout = crt_time + TIMEOUT_CHALLENGE;
	}

	strncat (msg, server->challenge, sizeof (msg) - strlen (msg) - 1);
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
	qbyte packet [2048] = "\xFF\xFF\xFF\xFF" M2C_GETSERVERSREPONSE "\\";
	size_t packetind;
	server_t* sv;
	unsigned int protocol;
	unsigned int sv_addr;
	unsigned short sv_port;
	qboolean no_empty;
	qboolean no_full;

	MsgPrint (MSG_NORMAL, "> %s ---> getservers\n", peer_address);

	// Check if there's a name before the protocol number
	// In this case, the message comes from a DarkPlaces-compatible client
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
	// Else, it comes from a Quake III Arena client
	else
	{
		strncpy (gamename, GAMENAME_Q3A, sizeof (gamename) - 1);
		gamename[sizeof (gamename) - 1] = '\0';
	}

	no_empty = (strstr (msg, "empty") == NULL);
	no_full = (strstr (msg, "full") == NULL);

	MsgPrint (MSG_DEBUG, "> %s <--- getserversResponse\n", peer_address);

	// Add every relevant server
	packetind = strlen (packet);
	for (sv = Sv_GetFirst (); sv != NULL; sv = Sv_GetNext ())
	{
		// Make sure we won't overflow the buffer
		if (packetind > sizeof (packet) - (7 + 6))
			break;

		sv_addr = ntohl (sv->address.sin_addr.s_addr);
		sv_port = ntohs (sv->address.sin_port);

		// Extra debugging info
		if (max_msg_level >= MSG_DEBUG)
		{
			MsgPrint (MSG_DEBUG,
					  "Comparing server: IP:\"%u.%u.%u.%u:%hu\", p:%u, c:%hu, g:\"%s\"\n",
					  sv_addr >> 24, (sv_addr >> 16) & 0xFF,
					  (sv_addr >>  8) & 0xFF, sv_addr & 0xFF,
					  sv_port, sv->protocol, sv->nbclients, sv->gamename);

			if (sv->protocol != protocol)
				MsgPrint (MSG_DEBUG,
						  "Reject: protocol %u != requested %u\n",
						  sv->protocol, protocol);
			if (sv->nbclients == 0 && no_empty)
				MsgPrint (MSG_DEBUG,
						  "Reject: nbclients is %hu/%hu && no_empty\n",
						  sv->nbclients, sv->maxclients);
			if (sv->nbclients == sv->maxclients && no_full)
				MsgPrint (MSG_DEBUG,
						  "Reject: nbclients is %hu/%hu && no_full\n",
						  sv->nbclients, sv->maxclients);
			if (gamename[0] && strcmp (gamename, sv->gamename))
				MsgPrint (MSG_DEBUG,
						  "Reject: gamename \"%s\" != requested \"%s\"\n",
						  sv->gamename, gamename);
		}

		// Check protocol, options, and gamename
		if (sv->protocol != protocol ||
			(sv->nbclients == 0 && no_empty) ||
			(sv->nbclients == sv->maxclients && no_full) ||
			(gamename[0] && strcmp (gamename, sv->gamename)))
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
				  "> ERROR: invalid infoResponse from %s (protocol: %d, maxclients: %d)\n",
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

	// Q3A doesn't send a gamename, so we add it manually
	if (value == NULL)
		value = GAMENAME_Q3A;
	strncpy (server->gamename, value, sizeof (server->gamename) - 1);

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
void HandleMessage (const qbyte* msg, size_t length,
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
