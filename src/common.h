/*
	common.h

	Common header file for dpmaster

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


#ifndef _COMMON_H_
#define _COMMON_H_


#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>


// ---------- Constants ---------- //

// Maximum and minimum sizes for a valid incoming packet
#define MAX_PACKET_SIZE_IN 2048
#define MIN_PACKET_SIZE_IN 5


// ---------- Types ---------- //

// A few basic types
typedef enum {false, true} qboolean;
typedef unsigned char qbyte;

// The various messages levels
typedef enum
{
	MSG_NOPRINT,	// used by "max_msg_level" (= no printings)
	MSG_ERROR,		// errors
	MSG_WARNING,	// warnings
	MSG_NORMAL,		// standard messages
	MSG_DEBUG		// for debugging purpose
} msg_level_t;

// Command line option
typedef struct
{
	const char* long_name;		// if NULL, this is the end of the list
	const char* help_syntax;	// help string printed by PrintHelp (syntax)
	const char* help_desc;		// help string printed by PrintHelp (description)
	int	help_param [2];			// optional parameters for the "help_desc" string
	char short_name;			// may be '\0' if it has no short name
	qboolean accept_param;		// "true" if the option may have 1 parameter
	qboolean need_param;		// "true" if the option requires 1 parameter
}  cmdlineopt_t;


// ---------- Public variables ---------- //

// The port we use dy default
extern unsigned short master_port;

// The current time (updated every time we receive a packet)
extern time_t crt_time;

// Maximum level for a message to be printed
extern msg_level_t max_msg_level;

// Peer address. We rebuild it every time we receive a new packet
extern char peer_address [128];


// ---------- Public functions ---------- //

// Print a message to screen, depending on its verbose level
void MsgPrint (msg_level_t msg_level, const char* format, ...);

// Returns a string containing the current date and time
const char* BuildDateString (void);


#endif  // #ifndef _COMMON_H_
