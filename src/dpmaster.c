
/*
	dpmaster.c

	A master server for DarkPlaces, Quake 3 Arena
	and any game supporting the DarkPlaces master server protocol

	Copyright (C) 2002-2008  Mathieu Olivier

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
#include "messages.h"
#include "servers.h"


// ---------- Constants ---------- //

// Version of dpmaster
#define VERSION "2.0-devel"

// Default master port
#define DEFAULT_MASTER_PORT 27950


// ---------- Private variables ---------- //

// Should we print the date before any new console message?
static qboolean print_date = false;

// Cross-platform command line options
static const cmdlineopt_t cmdline_options [] =
{
	{
		"allow-loopback",
		NULL,
		"Accept servers on loopback interfaces (for debugging purposes only)",
		{ 0, 0 },
		'\0',
		false,
		false
	},
	{
		"help",
		NULL,
		"This help text",
		{ 0, 0 },
		'h',
		false,
		false
	},
	{
		"hash-size",
		"<hash_size>",
		"Hash size in bits, up to %d (default: %d)",
		{ MAX_HASH_SIZE, DEFAULT_HASH_SIZE },
		'H',
		true,
		true
	},
	{
		"listen",
		"<address>",
		"Listen on local address <address>\n"
		"   You can listen on up to %d addresses",
		{ MAX_LISTEN_SOCKETS, 0 },
		'l',
		true,
		true
	},
	{
		"log",
		NULL,
		"Enable the logging to disk",
		{ 0, 0 },
		'L',
		false,
		false
	},
	{
		"log-file",
		"<file_path>",
		"Use <file_path> as the log file (default: " DEFAULT_LOG_FILE ")",
		{ 0, 0 },
		'\0',
		true,
		true
	},
	{
		"map",
		"<a1>=<a2>",
		"Map address <a1> to <a2> when sending it to clients\n"
		"   Addresses can contain a port number (ex: myaddr.net:1234)",
		{ 0, 0 },
		'm',
		true,
		true
	},
	{
		"max-servers",
		"<max_servers>",
		"Maximum number of servers recorded (default: %d)",
		{ DEFAULT_MAX_NB_SERVERS, 0 },
		'n',
		true,
		true
	},
	{
		"max-servers-per-addr",
		"<max_per_addr>",
		"Maximum number of servers per address (default: %d)\n"
		"   0 means there's no limit",
		{ DEFAULT_MAX_NB_SERVERS_PER_ADDRESS, 0 },
		'N',
		true,
		true
	},
	{
		"port",
		"<port_num>",
		"Default network port (default value: %d)",
		{ DEFAULT_MASTER_PORT, 0 },
		'p',
		true,
		true
	},
	{
		"verbose",
		"[verbose_lvl]",
		"Verbose level, up to %d (default: %d; no value means max)",
		{ MSG_DEBUG, MSG_NORMAL },
		'v',
		true,
		false
	},
	{
		NULL,
		NULL,
		NULL,
		{ 0, 0 },
		'\0',
		false,
		false
	}
};

// Log file path
static const char* log_filepath = DEFAULT_LOG_FILE;

// Should we (re)open the log file?
static volatile qboolean must_open_log = false;

// Should we close the log file?
static volatile qboolean must_close_log = false;

// The log file
static FILE* log_file = NULL;


// ---------- Public variables ---------- //

// The port we use by default
unsigned short master_port = DEFAULT_MASTER_PORT;

// The current time (updated every time we receive a packet)
time_t crt_time;

// Maximum level for a message to be printed
msg_level_t max_msg_level = MSG_NORMAL;

// Peer address. We rebuild it every time we receive a new packet
char peer_address [128];


// ---------- Private functions ---------- //

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
UnsecureInit

System independent initializations, called BEFORE the security initializations.
We need this intermediate step because DNS requests may not be able to resolve
after the security initializations, due to chroot.
====================
*/
static qboolean UnsecureInit (void)
{
	// Resolve the address mapping list
	if (! Sv_ResolveAddressMappings ())
		return false;

	// Resolve the listening socket addresses
	if (! Sys_ResolveListenAddresses ())
		return false;

	return true;
}


/*
====================
Cmdline_Option

Parse a system-independent command line option
"param" may be NULL, if the option doesn't need a parameter
====================
*/
static qboolean Cmdline_Option (const cmdlineopt_t* opt, const char* param)
{
	const char* opt_name;
	
	assert (param == NULL || opt->accept_param);
	assert (param != NULL || ! opt->need_param);

	opt_name = opt->long_name;

	// Are servers on loopback interfaces allowed?
	if (strcmp (opt_name, "allow-loopback") == 0)
		allow_loopback = true;

	// Help
	else if (strcmp (opt_name, "help") == 0)
		return false;

	// Hash size
	else if (strcmp (opt_name, "hash-size") == 0)
	{
		const char* start_ptr;
		char* end_ptr;
		unsigned int hash_size;

		start_ptr = param;
		hash_size = (unsigned int)strtol (start_ptr, &end_ptr, 0);
		if (end_ptr == start_ptr || *end_ptr != '\0')
			return false;

		return Sv_SetHashSize (hash_size);
	}

	// Listen address
	else if (strcmp (opt_name, "listen") == 0)
	{
		if (param[0] == '\0')
			return false;

		return Sys_DeclareListenAddress (param);
	}

	// Log
	else if (strcmp (opt_name, "log") == 0)
		must_open_log = true;

	// Log file
	else if (strcmp (opt_name, "log-file") == 0)
	{
		if (param[0] == '\0')
			return false;

		log_filepath = param;
	}

	// Address mapping
	else if (strcmp (opt_name, "map") == 0)
		return Sv_AddAddressMapping (param);

	// Maximum number of servers
	else if (strcmp (opt_name, "max-servers") == 0)
	{
		const char* start_ptr;
		char* end_ptr;
		unsigned int max_nb_servers;

		start_ptr = param;
		max_nb_servers = (unsigned int)strtol (start_ptr, &end_ptr, 0);
		if (end_ptr == start_ptr || *end_ptr != '\0')
			return false;
		
		return Sv_SetMaxNbServers (max_nb_servers);
	}

	// Maximum number of servers per address
	else if (strcmp (opt_name, "max-servers-per-addr") == 0)
	{
		const char* start_ptr;
		char* end_ptr;
		unsigned int max_per_address;
		
		start_ptr = param;
		max_per_address = (unsigned int)strtol (start_ptr, &end_ptr, 0);
		if (end_ptr == start_ptr || *end_ptr != '\0')
			return false;
		
		return Sv_SetMaxNbServersPerAddress (max_per_address);
	}

	// Port number
	else if (strcmp (opt_name, "port") == 0)
	{
		const char* start_ptr;
		char* end_ptr;
		unsigned short port_num;
		
		start_ptr = param;
		port_num = (unsigned short)strtol (start_ptr, &end_ptr, 0);
		if (end_ptr == start_ptr || *end_ptr != '\0' || port_num == 0)
			return false;

		master_port = port_num;
	}

	// Verbose level
	else if (strcmp (opt_name, "verbose") == 0)
	{
		// If a verbose level has been specified
		if (param != NULL)
		{
			const char* start_ptr;
			char* end_ptr;
			unsigned int vlevel;

			start_ptr = param;
			vlevel = (unsigned int)strtol (start_ptr, &end_ptr, 0);
			if (end_ptr == start_ptr || *end_ptr != '\0' ||
				vlevel > MSG_DEBUG)
				return false;
			max_msg_level = vlevel;
		}
		else
			max_msg_level = MSG_DEBUG;
	}

	return true;
}


/*
====================
ParseCommandLine

Parse the options passed by the command line
====================
*/
static qboolean ParseCommandLine (int argc, const char* argv [])
{
	int ind = 1;
	qboolean valid_options = true;

	while (ind < argc && valid_options)
	{
		const char* crt_arg = argv[ind];

		valid_options = false;

		// If it doesn't even look like an option, why bother?
		if (crt_arg[0] == '-' && crt_arg[1] != '\0')
		{
			const cmdlineopt_t* cmdline_opt = NULL;
			const char* param = NULL;
			qboolean sys_option;

			// If it's a long option
			if (crt_arg[1] == '-')
			{
				const char* equal_char;
				char option_name [64];
				unsigned int cmd_ind;

				// Extract the option, and its attached parameter if any
				equal_char = strchr (&crt_arg[2], '=');
				if (equal_char != NULL)
				{
					size_t opt_size = equal_char - &crt_arg[2];

					// If it's an invalid option
					if (opt_size <= 0 || opt_size >= sizeof (option_name))
						break;

					memcpy (option_name, &crt_arg[2], opt_size);
					option_name[opt_size] = '\0';

					param = equal_char + 1;
				}
				else
				{
					strncpy (option_name, &crt_arg[2], sizeof (option_name) - 1);
					option_name[sizeof (option_name) - 1] = '\0';
				}

				// Cross-platform options
				for (cmd_ind = 0; cmdline_options[cmd_ind].long_name != NULL; cmd_ind++)
					if (strcmp (cmdline_options[cmd_ind].long_name, option_name) == 0)
					{
						cmdline_opt = &cmdline_options[cmd_ind];
						sys_option = false;
						break;
					}

				if (cmdline_opt == NULL)
				{
					// System-dependent options
					for (cmd_ind = 0; sys_cmdline_options[cmd_ind].long_name != NULL; cmd_ind++)
						if (strcmp (sys_cmdline_options[cmd_ind].long_name, option_name) == 0)
						{
							cmdline_opt = &sys_cmdline_options[cmd_ind];
							sys_option = true;
							break;
						}
				}
			}

			// If it's a short option
			else
			{
				const char short_cmd = crt_arg[1];
				unsigned int cmd_ind;

				// Extract the attached parameter if any
				assert (crt_arg[1] != '\0');
				if (crt_arg[2] != '\0')
					param = &crt_arg[2];

				// Cross-platform options
				for (cmd_ind = 0; cmdline_options[cmd_ind].long_name != NULL; cmd_ind++)
					if (cmdline_options[cmd_ind].short_name == short_cmd)
					{
						cmdline_opt = &cmdline_options[cmd_ind];
						sys_option = false;
						break;
					}

				if (cmdline_opt == NULL)
				{
					// System-dependent options
					for (cmd_ind = 0; sys_cmdline_options[cmd_ind].long_name != NULL; cmd_ind++)
						if (sys_cmdline_options[cmd_ind].short_name == short_cmd)
						{
							cmdline_opt = &sys_cmdline_options[cmd_ind];
							sys_option = true;
							break;
						}
				}

			}

			if (cmdline_opt != NULL)
			{
				qboolean has_param;

				has_param = (param != NULL || (ind + 1 < argc &&
							 argv[ind + 1][0] != '\0' && argv[ind + 1][0] != '-'));

				// Check the number of parameters
				if ((! cmdline_opt->need_param || has_param) &&
					(cmdline_opt->accept_param || ! has_param))
				{
					if (has_param && param == NULL)
					{
						ind++;
						param = argv[ind];
					}

					if (sys_option)
						valid_options = Sys_Cmdline_Option (cmdline_opt, param);
					else
						valid_options = Cmdline_Option (cmdline_opt, param);

					ind++;
				}
			}
		}
	}

	// If the command line is not OK, reset the verbose level
	// to make sure the help text will be printed
	if ( ! valid_options)
		max_msg_level = MSG_NORMAL;

	return valid_options;
}


/*
====================
PrintCmdlineOptionsHelp

Print the help text for a pool of command line options
====================
*/
static void PrintCmdlineOptionsHelp (const char* pool_name, const cmdlineopt_t* opts)
{
	if (opts[0].long_name != NULL)
	{
		unsigned int cmd_ind;

		MsgPrint (MSG_ERROR,"Available %s options are:\n", pool_name);

		for (cmd_ind = 0; opts[cmd_ind].long_name != NULL; cmd_ind++)
		{
			const cmdlineopt_t* crt_cmd = &opts[cmd_ind];
			qboolean has_short_name = (crt_cmd->short_name != '\0');

			// Short name, if any
			if (has_short_name)
			{
				MsgPrint (MSG_ERROR, " * -%c", crt_cmd->short_name);
				if (crt_cmd->help_syntax != NULL)
					MsgPrint (MSG_ERROR, " %s", crt_cmd->help_syntax);
				MsgPrint (MSG_ERROR, "\n");
			}
			
			// Long name
			MsgPrint (MSG_ERROR, " %c --%s",
					  has_short_name ? ' ' : '*', crt_cmd->long_name);
			if (crt_cmd->help_syntax != NULL)
				MsgPrint (MSG_ERROR, " %s", crt_cmd->help_syntax);
			MsgPrint (MSG_ERROR, "\n");

			// Description
			MsgPrint (MSG_ERROR, "   ");
			MsgPrint (MSG_ERROR, crt_cmd->help_desc,
					  crt_cmd->help_param[0], crt_cmd->help_param[1]);
			MsgPrint (MSG_ERROR, "\n");

			MsgPrint (MSG_ERROR, "\n");
		}
	}
}


/*
====================
PrintHelp

Print the command line syntax and the available options
====================
*/
static void PrintHelp (void)
{
	MsgPrint (MSG_ERROR, "\nSyntax: dpmaster [options]\n\n");

	PrintCmdlineOptionsHelp ("cross-platform", cmdline_options);
	PrintCmdlineOptionsHelp ("platform-specific", sys_cmdline_options);
}


/*
====================
SignalHandler

Handling of the signals sent to this processus
====================
*/
#if defined(SIGUSR1) || defined(SIGUSR2)
static void SignalHandler (int Signal)
{
	switch (Signal)
	{
#ifdef SIGUSR1
		case SIGUSR1:
			must_open_log = true;
			break;
#endif
#ifdef SIGUSR2
		case SIGUSR2:
			must_close_log = true;
			break;
#endif
		default:
			// We aren't suppose to be here...
			assert(false);
			break;
	}
}
#endif


/*
====================
SecureInit

System independent initializations, called AFTER the security initializations
====================
*/
static qboolean SecureInit (void)
{
	// Init the time and the random seed
	crt_time = time (NULL);
	srand ((unsigned int)crt_time);

#ifdef SIGUSR1
	if (signal (SIGUSR1, SignalHandler) == SIG_ERR)
	{
		MsgPrint (MSG_ERROR, "> ERROR: can't capture the SIGUSR1 signal\n");
		return false;
	}
#endif
#ifdef SIGUSR2
	if (signal (SIGUSR2, SignalHandler) == SIG_ERR)
	{
		MsgPrint (MSG_ERROR, "> ERROR: can't capture the SIGUSR2 signal\n");
		return false;
	}
#endif

	if (! Sys_CreateListenSockets ())
		return false;
	
	// If there no socket to listen to for whatever reason, there's simply nothing to do
	if (nb_sockets <= 0)
	{
		MsgPrint (MSG_ERROR, "> ERROR: there's no listening socket. There's nothing to do\n");
		return false;
	}

	// Initialize the server list and hash table
	if (! Sv_Init ())
		return false;

	return true;
}


/*
====================
CloseLogFile

Close the log file
====================
*/
static void CloseLogFile (const char* datestring)
{
	must_close_log = false;

	if (log_file != NULL)
	{
		if (datestring == NULL)
			datestring = BuildDateString();

		fprintf (log_file, "\n> Closing log file (time: %s)\n", datestring);
		fclose (log_file);
		log_file = NULL;
	}
}


/*
====================
UpdateLogStatus

Enable / disable the logging, depending on the variable "must_open_log"
====================
*/
static qboolean UpdateLogStatus (qboolean init)
{
	// If we need to (re)open the log file
	if (must_open_log)
	{
		const char* datestring;

		must_open_log = false;

		datestring = BuildDateString ();
		CloseLogFile (datestring);

		log_file = fopen (log_filepath, "a");
		if (log_file == NULL)
			return false;

		// Make the log stream fully buffered (instead of line buffered)
		setvbuf (log_file, NULL, _IOFBF, 0);

		fprintf (log_file, "> Opening log file (time: %s)\n", datestring);

		// if we're opening the log after the initialization, print the list of servers
		if (! init)
			Sv_PrintServerList (MSG_WARNING);

	}

	// If we need to close the log file
	if (must_close_log)
	{
		CloseLogFile (NULL);
	}

	return true;
}


/*
====================
main

Main function
====================
*/
int main (int argc, const char* argv [])
{
	qboolean valid_options;

	// Get the options from the command line
	valid_options = ParseCommandLine (argc, argv);

	MsgPrint (MSG_NORMAL,
			  "dpmaster, a master server supporting the DarkPlaces\n"
			  "and Quake III Arena master server protocols\n"
			  "(version " VERSION ", compiled the " __DATE__ " at " __TIME__ ")\n");

	// If there was a mistake in the command line, print the help and exit
	if (!valid_options)
	{
		PrintHelp ();
		return EXIT_FAILURE;
	}

	// Start the log if necessary
	if (! UpdateLogStatus (true))
		return EXIT_FAILURE;

	crt_time = time (NULL);
	print_date = true;

	// Initializations
	if (! Sys_UnsecureInit () || ! UnsecureInit () ||
		! Sys_SecurityInit () ||
		! Sys_SecureInit () || ! SecureInit ())
		return EXIT_FAILURE;

	// Until the end of times...
	for (;;)
	{
		fd_set sock_set;
		int max_sock;
		size_t sock_ind;
		int nb_sock_ready;

		FD_ZERO(&sock_set);
		max_sock = -1;
		for (sock_ind = 0; sock_ind < nb_sockets; sock_ind++)
		{
			int crt_sock = listen_sockets[sock_ind].socket;

			FD_SET(crt_sock, &sock_set);
			if (max_sock < crt_sock)
				max_sock = crt_sock;
		}

		// Flush the console and log file
		if (log_file != NULL)
			fflush (log_file);
		if (daemon_state < DAEMON_STATE_EFFECTIVE)
			fflush (stdout);

		nb_sock_ready = select (max_sock + 1, &sock_set, NULL, NULL, NULL);

		// Update the current time
		crt_time = time (NULL);

		print_date = false;
		UpdateLogStatus (false);

		// Print the date once per select()
		print_date = true;

		if (nb_sock_ready <= 0)
		{
			if (Sys_GetLastNetError() != NETERR_INTR)
				MsgPrint (MSG_WARNING,
						  "> WARNING: \"select\" returned %d\n", nb_sock_ready);
			continue;
		}

		for (sock_ind = 0;
			 sock_ind < nb_sockets && nb_sock_ready > 0;
			 sock_ind++)
		{
			struct sockaddr_storage address;
			socklen_t addrlen;
			int nb_bytes;
			char packet [MAX_PACKET_SIZE_IN + 1];  // "+ 1" because we append a '\0'
			int crt_sock = listen_sockets[sock_ind].socket;

			if (! FD_ISSET (crt_sock, &sock_set))
				continue;
			nb_sock_ready--;

			// Get the next valid message
			addrlen = sizeof (address);
			nb_bytes = recvfrom (crt_sock, packet, sizeof (packet) - 1, 0,
								 (struct sockaddr*)&address, &addrlen);

			if (nb_bytes <= 0)
			{
				MsgPrint (MSG_WARNING,
						  "> WARNING: \"recvfrom\" returned %d\n", nb_bytes);
				continue;
			}

			// If we may print something, rebuild the peer address string
			if (max_msg_level > MSG_NOPRINT &&
				(log_file != NULL || daemon_state < DAEMON_STATE_EFFECTIVE))
			{
				strncpy (peer_address, Sys_SockaddrToString(&address),
						 sizeof (peer_address));
				peer_address[sizeof (peer_address) - 1] = '\0';
			}

			// We print the packet contents if necessary
			if (max_msg_level >= MSG_DEBUG)
			{
				MsgPrint (MSG_DEBUG, "> New packet received from %s: ",
						  peer_address);
				PrintPacket ((qbyte*)packet, nb_bytes);
			}

			// A few sanity checks
			if (address.ss_family != AF_INET && address.ss_family != AF_INET6)
			{
				MsgPrint (MSG_WARNING,
						  "> WARNING: rejected packet from %s (invalid address family: %hd)\n",
						  peer_address, address.ss_family);
				continue;
			}
			if (Sys_GetSockaddrPort(&address) == 0)
			{
				MsgPrint (MSG_WARNING,
						  "> WARNING: rejected packet from %s (source port = 0)\n",
						  peer_address);
				continue;
			}
			if (nb_bytes < MIN_PACKET_SIZE_IN)
			{
				MsgPrint (MSG_WARNING,
						  "> WARNING: rejected packet from %s (size = %d bytes)\n",
						  peer_address, nb_bytes);
				continue;
			}
			if (*((unsigned int*)packet) != 0xFFFFFFFF)
			{
				MsgPrint (MSG_WARNING,
						  "> WARNING: rejected packet from %s (invalid header)\n",
						  peer_address);
				continue;
			}

			// Append a '\0' to make the parsing easier
			packet[nb_bytes] = '\0';

			// Call HandleMessage with the remaining contents
			HandleMessage (packet + 4, nb_bytes - 4, &address, addrlen, crt_sock);
		}
	}
}


// ---------- Public functions ---------- //

/*
====================
BuildDateString

Return a string containing the current date and time
====================
*/
const char* BuildDateString (void)
{
	static char datestring [80];

	strftime (datestring, sizeof(datestring), "%Y-%m-%d %H:%M:%S %Z",
			  localtime(&crt_time));
	return datestring;
}


/*
====================
MsgPrint

Print a message to screen, depending on its verbose level
====================
*/
void MsgPrint (msg_level_t msg_level, const char* format, ...)
{
	va_list args;

	// If the message level is above the maximum level, or if we output
	// neither to the console nor to a log file, there nothing to do
	if (msg_level > max_msg_level ||
		(log_file == NULL && daemon_state == DAEMON_STATE_EFFECTIVE))
		return;

	// Print a time stamp if necessary
	if (print_date)
	{
		const char* datestring = BuildDateString();

		if (daemon_state < DAEMON_STATE_EFFECTIVE)
			printf ("\n* %s\n", datestring);
		if (log_file != NULL)
			fprintf (log_file, "\n* %s\n", datestring);

		print_date = false;
	}

	va_start (args, format);
	if (daemon_state < DAEMON_STATE_EFFECTIVE)
		vprintf (format, args);
	if (log_file != NULL)
		vfprintf (log_file, format, args);
	va_end (args);
}
