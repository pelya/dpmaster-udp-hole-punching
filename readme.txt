
                         Dpmaster, an open master server
                         -------------------------------

                               General information
                               -------------------


 1) INTRODUCTION
 2) SYNTAX & OPTIONS
 3) BASIC USAGE
 4) SECURITY
 5) OUTPUT AND VERBOSITY LEVELS
 6) LOGGING
 7) GAME POLICY
 8) GAME PROPERTIES
 9) FLOOD PROTECTION
10) ADDRESS MAPPING
11) LISTENING INTERFACES
12) VERSION HISTORY
13) CONTACTS & LINKS


1) INTRODUCTION:

Dpmaster is a lightweight master server written from scratch for LordHavoc's
game engine DarkPlaces. It is an open master server because of its free source
code and documentation, and because its Quake III Arena-like protocol allows it
to fully support new games without having to restart or reconfigure it: start
and forget. In addition to its own protocol, dpmaster also supports the master
protocols of "Quake III Arena" (Q3A), "Return to Castle Wolfenstein" (RtCW), and
"Wolfenstein: Enemy Territory" (WoET).

Several game engines currently support the DP master server protocol: DarkPlaces
and all its derived games (such as Nexuiz and Transfusion), QFusion and most of
its derived games (such as Warsow), and FTE QuakeWorld. Also, IOQuake3 uses it
for its IPv6-enabled servers and clients since its version 1.36. Finally,
dpmaster's source code has been used by a few projects as a base for creating
their own master servers (this is the case of Tremulous, for instance).

If you want to use the DP master protocol in one of your software, take a look
at the section "USING DPMASTER WITH YOUR GAME" in "doc/techinfo.txt" for further
explanations. It is pretty easy to implement, and if you ask politely, chances
are you will be able to find someone that will let you use his running dpmaster
if you can't get your own.

Although dpmaster is being primarily developed on a Linux PC, it is regularly
compiled and tested on Windows XP, OpenBSD, and Mac OS X. It has also been run
successfully on FreeBSD, NetBSD and Windows 2000 in the past, but having no
regular access to any of those systems, I cannot guarantee that it is still the
case. In particular, building dpmaster on Windows 2000 may require some minor
source code changes due to the recent addition of IPv6 support in dpmaster,
Windows 2000 having a limited support for this protocol.

Take a look at the "COMPILING DPMASTER" section in "doc/techinfo.txt" for more
practical information on how to build it.

The source code of dpmaster is available under the GNU General Public License,
version 2. You can find the text of this license in the file "doc/license.txt".


2) SYNTAX & OPTIONS:

The syntax of the command line is the classic: "dpmaster [options]". Running
"dpmaster -h" will print the available options for your version. Be aware that
some options are only available on UNIXes, including all security-related
options - see the "SECURITY" section below.

All options have a long name (a string), and most of them also have a short name
(one character). In the command line, long option names are preceded by 2
hyphens and short names by 1 hyphen. For instance, you can run dpmaster as a
daemon on UNIX systems by calling either "dpmaster -D" or "dpmaster --daemon".

A lot of options have one or more associated parameters, separated from the
option name and from each other by a blank space. Optionally, you are allowed
to simply append the first parameter to an option name if it is in its short
form, or to separate it from the option name using an equal sign if it is in its
long form. For example, these 4 ways of running dpmaster with a maximum number
of servers of 16 are equivalent:

   * dpmaster -n 16
   * dpmaster --max-servers 16
   * dpmaster -n16
   * dpmaster --max-servers=16


3) BASIC USAGE:

For most users, simply running dpmaster, without any particular parameter,
should work perfectly. Being an open master server, it does not require any
game-related configuration. The vast majority of dpmaster's options deal with
how you want to run it: which network interfaces to use, how many servers it
will accept, where to put the log file .... and all those options have default
values that should suit almost everyone.

That being said, here are a few options you may find handy.

The most commonly used one is probably "-D" (or "--daemon"), a UNIX-specific
option to make the program run in the background, as a daemon process.

If you intent to run dpmaster for a long period of time, you may want to take a
look at the log-related options before starting it (see the LOGGING section). In
particular, make sure you have write permission in the directory the log file is
supposed to be written.

Finally, you can use the classic verbose option "-v" to make dpmaster print
extra information (see "OUTPUT AND VERBOSITY LEVELS" below for more).


4) SECURITY:

First, you shouldn't be afraid to run dpmaster on your machine: at the time I
wrote those lines, only one security warning has been issued since the first
release of dpmaster. It has always been developed with security in mind and will
always be.

Also, dpmaster needs very few things to run in its default configuration. A
little bit of memory, a few CPU cycles from time to time and a network port are
its only basic requirements. So feel free to restrict its privileges as much as
you can.

The UNIX/Linux version of dpmaster has even a builtin security mechanism that
triggers when it is run with super-user (root) privileges. Basically, the
process locks (chroots) itself in the directory "/var/empty/" and drops its
privileges in favor of those of user "nobody". This path and this user name are
of course customizable, thanks to the '-j' and '-u' command line options.

If you are running dpmaster on a Windows system, you may want to add a
"dpmaster" user on your computer. Make it a normal user, not a power user or an
administrator. You'll then be able to run dpmaster using this low-privilege
account. Right click on "dpmaster.exe" while pressing the SHIFT button; select
"Run as...", and type "dpmaster", the password you chose for it, and your
domain main (your computer name probably). The same result can also be achieved
by using Windows' "runas" command.


5) OUTPUT AND VERBOSITY LEVELS:

The "-v" / "--verbose" option allows you to control the amount of text dpmaster
outputs. Setting its verbosity to a particular level make dpmaster output all
texts belonging to that level or below. If you don't specify a verbose level
right after the "-v" command line option, the highest level will be used. 

There are 5 verbose levels:
   * 0: No output, except if the parsing of the command line fails.
   * 1: Fatal errors only. It is almost similar to level 0 since fatal errors
        mostly occur during the parsing of the command line in this version.
   * 2: Warnings, including non-fatal system errors, malformed network messages,
        unexpected events (when the maximum number of servers is reached for
        instance), and the server list printed on top of log files.
   * 3: The default level. Standard printings, describing the current activity.
   * 4: All information (a lot!), mostly helpful when trying to debug a problem.

Looking for errors in a level 4 log can be a tedious task. To make your job
easier, all error messages in dpmaster start with the word "ERROR" in capital
letters, and all warning messages start with the word "WARNING", again in
capital letters.


6) LOGGING:

You can enable logging by adding "-L" or "--log" to the command line. The
default name of the log file is "dpmaster.log", either in the working directory
for Windows systems, or in the "/var/log" directory for UNIX systems. You can
change the path and name of this file using the "--log-file" option.

The obvious way to use the log is to enable it by default. But if you want to do
that, you may want to consider using a lesser verbose level ("-v" or
"--verbose", with a value of 1 - only errors, or 2 - only errors and warnings),
as dpmaster tends to be very verbose at its default level (3) or higher.

Another way to use the log is to set the verbose level to its maximum value, but
to enable the log only when needed, and then to disable it afterwards. This is
possible on systems that provide POSIX signals USR1 and USR2 (all supported
systems except the Windows family). When dpmaster receives the USR1 signal, it
opens its log file, or reopens it if it was already opened, dumps the list of
all registered servers, and then proceeds with its normal logging. When it
receives the USR2 signal, it closes its log file.

Note that dpmaster will never overwrite an existing log file, it always appends
logs to it. It prevents you from losing a potentially important log by mistake,
with the drawback of having to clean the logs manually from time to time.

There are a couple of pitfalls you should be aware of when using a log file:
first, if you run dpmaster as a daemon, remember that its working directory is
the root directory, so be careful with relative paths. And second, if you put
your dpmaster into a chroot jail, and start or restart the log after the
initialization phase, its path will then be rooted and relative to the jail root
directory.


7) GAME POLICY:

If you run an instance of dpmaster, we strongly encourage you to let it open to
any game or player. Dpmaster has been developed for this particular usage and is
well-suited for it.

That said, if you want to restrict which games are allowed on your master, you
can use the "--game-policy" option. It makes dpmaster explicitly accept or
reject network messages based on the game they are related to. For example:

        dpmaster --game-policy accept Quake3Arena Transfusion

will force dpmaster to accept servers, and answer to requests, only when they're
related to either Q3A or Transfusion. At the opposite:

        dpmaster --game-policy reject AnnoyingGame

will accept any game messages except those related to AnnoyingGame.

You can have multiple "--game-policy" lists on the same command line, but they
must all use the same policy (either "accept" or "reject").

As you can see in the first example, "Quake3Arena" is the name you'll have to
use for Q3A. The other game names only depend on what code names their
respective engines choose to advertise their servers and to make their client
requests.

Two final warnings regarding this option. First, be careful, the names are case-
sensitive. And second, this option expects at least 2 parameters (accept/reject,
and at least one game name), so this:

        dpmaster --game-policy accept -v -n 200

will make dpmaster accept messages only when they will be related to a game
called "-v" (certainly not what you want...).


8) GAME PROPERTIES:

Dpmaster supports 2 kinds of games: open-source games which use the DarkPlaces
master protocol, and a few formerly closed-source games which use the Quake 3
master protocol or a variant of it. The DarkPlaces master protocol itself is a
variant of the Quake 3 master protocol, the main difference being that games
send their name in addition to the usual informations or queries. That's what
makes dpmaster able to support multiple games easily.

Unfortunately, formerly closed-source games don't always send this information,
or another information that allows dpmaster to guess the game name safely.
That's why we call them "anonymous games" here. Up to version 2.1, the only
anonymous game dpmaster supported was Q3A, so it was easy: if the game didn't
send its name, it was Q3A. But starting from version 2.2, dpmaster also supports
2 other anonymous games: RtCW and WoET. That's why a new mechanism had to be
created to allow dpmaster to figure out which game sends it which message. This
mechanism is called "game properties".

Game properties are controlled by the command line option "--game-properties"
(short option: "-g"). A number of properties are built into dpmaster, so you
shouldn't have to configure anything for a standard usage. You can make it print
its current list of game properties by using the command line option without any
parameter. Here's the current output you get at the time I write those lines:

        Game properties:
        * et:
           - protocols: 72, 80, 83, 84
           - options: send-empty-servers, send-full-servers
           - heartbeats: EnemyTerritory-1 (alive), ETFlatline-1 (dead)

        * wolfmp:
           - protocols: 50, 59, 60
           - options: none
           - heartbeats: Wolfenstein-1 (alive), WolfFlatline-1 (dead)

        * Quake3Arena:
           - protocols: 66, 67, 68
           - options: none
           - heartbeats: QuakeArena-1 (alive)

"et", "wolfmp" and "Quake3Arena" are the respective game names for WoET, RtCW
and Q3A. Each of them have been assigned several protocol numbers, options, and
up to 2 heartbeat tags (one for alive servers, one for dying servers). All these
values are optional: a game name can have no protocol, no option and no tag
associated to it, although there would be no point to that.

Normal (alive) heartbeat tags are used to figure out the game name when servers
don't send it, like those of Q3A and some old Wolfenstein versions. Dead
heartbeat tags are simply ignored, they don't trigger the sending of a "getinfo"
message, unlike normal heartbeats.

Protocol numbers are used to figure out the game name when clients don't send it
with their "getservers" requests, and unfortunately this is the case for all the
anonymous games currently supported. If the protocol declared by the client
doesn't match any of the registered protocol numbers, dpmaster will use the
first server of an anonymous game it will find, that uses this very protocol
number, as the reference for the name. In other words, it will handle the query
as if it has declared the same game name as this server.

Options allows you to specify non-standard behaviours for a game. For example,
the WoET's clients expect the master server to send them the complete list of
servers, even though they don't specify that they want empty and full servers,
like other Q3A-derived games do. By associating the proper options to its game
name ("et"), we make sure that dpmaster will send the expected list anyway.
The available options are: "send-empty-servers" and "send-full-servers".

In order to modify the properties of a game, you have to use the command line
option, with the game name as the first parameter, and then the modifications
you want. You can either assign new values to a property (using "="), add values
to it (using "+="), or remove values from it (using "-="). The values in the
list must be separated by commas. No spaces are allowed, neither in the game
name, nor in the list of modifications. The available properties are:
"protocols", "options", "heartbeat" (normal heartbeat), and "flatline" (dying
heartbeat).

And you can have multiple game property changes in your command line, obviously.
Here are a few examples.

To add protocol 70 and a dead heartbeat to Q3A:

        dpmaster -g Quake3Arena protocols+=70 flatline=Q3ADeadHB

To remove all protocols from RtCW and give it 2 brand new ones, 4321 and 1234:

        dpmaster -g wolfmp protocols=4321,1234

To not send full servers to WoET clients, and to remove protocol 50 from RtCW:

        dpmaster -g et options-=send-full-servers -g wolfmp protocols-=50

The game properties has been added to dpmaster in order to support anonymous
games, but it can also be useful for other games. For instance, you can force
dpmaster to send empty servers to Warsow clients like this:

        dpmaster -g Warsow options=send-empty-servers

You could also specify a list of protocol numbers here, but since Warsow uses
the DarkPlaces master protocol, both its clients and servers declares their game
names, so it would be useless.

Note that you can ask for the list of properties after you have declared some
modifications, using a final "-g" on the command line. In this case, the printed
list will contain your modifications. It's a good way to check that you didn't
make any mistake before actually running your master server.


9) FLOOD PROTECTION:

If the master server you run has to handle a lot of clients, you will probably
be interested in the flood protection mechanism Timothee Besset contributed to
dpmaster version 2.2.

Its purpose is to protect the master server bandwidth, by temporary ignoring the
requests of clients which have already made several ones in the few seconds
before. More precisely, a client can only make a limited number of requests (up
to a "throttle limit") before it is only allowed one request every X seconds (X
is called the "decay time"). A simple way to view it is to imagine that each
client has initially - and at most - a number of tokens equal to the throttle
limit minus 1. It must use/give one token for each request he does, but it
regains tokens over time, 1 token every 3 seconds for instance if the decay time
is set to 3. So for example, with a throttle limit of 5 and a decay time of 3,
a client could do 5 - 1 = 4 requests in a row before having to wait 3 seconds
between its requests, or they will not be answered.

This protection is disabled by default because, by definition, it can disturb
the service provided to the master's users, and given most master servers don't
have to deal with this type of flood problem, it would be for no real benefits.
You can enable the protection by passing the option "-f" or "--flood-protection"
in the command line. The throttle limit and decay time can be modified with
"--fp-throttle" and "--fp-decay-time" respectively.

You also have the possibility to tune the maximum number of client records and
the client hash size with "--max-clients" and "--cl-hash-size". But since client
records are reused extremely rapidly in this mechanism, chances are the default
values will be way bigger than your actual needs anyway.


10) ADDRESS MAPPING:

Address mapping allows you to tell dpmaster to transmit an IPv4 address instead
of another one to the clients, in the "getserversResponse" messages. It can be
useful in several cases. Imagine for instance that you have a dpmaster and a
server behind a firewall, with local IPv4 addresses. You don't want the master
to send the local server IP address. Instead, you probably want it to send the
firewall address.

Address mappings are currently only available for IPv4 addresses. It appears
IPv6 doesn't need such a mechanism, since NATs have been deprecated in this new
version of the protocol. However, feel free to contact me if you actually need
IPv6 address mappings for some reason.

Address mappings are declared on the command line, using the "-m" or "--map"
option. You can declare as many of them as you want. The syntax is:

        dpmaster -m address1=address2 -m address3=address4 ...

An address can be an explicit IPv4 address, such as "192.168.1.1", or an host
name, "www.mydomain.net" for instance. Optionally, a port number can be
appended after a ':' (ex: "www.mydomain.net:1234").

The most simple mappings are host-to-host mappings. For example:

        dpmaster -m 1.2.3.4=myaddress.net

In this case, each time dpmaster would have transmitted "1.2.3.4" to a client,
it will transmit the "myaddress.net" IP address instead.

If you add a port number to the first address, then the switching will only
occur if the server matches the address and the port number.
If you add a port number to the second address, then dpmaster will not only
change the IP address, it will also change the port number.

So there are 4 types of mappings:
    - host1 -> host2 mappings:
        They're simple, we just saw them.

    - host1:port1 -> host2:port2 mappings:
        If the server matches exactly the 1st address, it will be transmitted
        as the 2nd address.

    - host1:port1 -> host2 mappings:
        If the server matches exactly the 1st address, its IP address will be
        transmitted as the "host2" IP address. The port number won't change.
        It's equivalent to "host1:port1=host2:port1".

    - host1 -> host2:port2 mappings
        If the server is hosted on host1, its address will be transmitted as
        "host2:port2".

Finally, be aware that you can't declare an address mapping from or to
"0.0.0.0", neither can you declare an address mapping to a loopback address
(i.e. 127.x.y.z:p). Mapping from a loopback address is permitted though, and
it's actually one of the 2 only ways to make dpmaster accept a server talking
from a loopback address (the other way being a command line option used for
test purposes - do NOT run your master with this option!).


11) LISTENING INTERFACES:

By default, dpmaster creates one IPv4 socket and one IPv6 socket (if IPv6
support is available of course). It will listen on every network interface, on
the default port unless you specified another one using "-p" or "--port". If
you want it to listen on one or more particular interface(s) instead, you will
have to use the command line option "-l" or "--listen".

Running dpmaster with no "-l" option is (almost) like running it with:

        dpmaster --listen 0.0.0.0 --listen ::

The first option is for listening on all IPv4 interfaces, the second for
listening on all IPv6 interfaces, both on the default port. The only
difference between this command line and one without any "--listen" option is
that dpmaster will abort in the former if IPv4 or IPv6 isn't supported by your
system, as you have explicitly requested those network sockets to be opened.
Note that if you don't want dpmaster to listen on IPv6 interfaces, you can
easily do it by only specifying "-l 0.0.0.0" on the command line.

As usual, you can specify a port number along with an address, by appending
":" and then the port. In this case, numeric IPv6 addresses need to be put
between brackets first, so that dpmaster won't get confused when interpreting
the various colons. For example:

        dpmaster -l an.address.net:546 -l [2000::1234:5678]:890

will make dpmaster listen on the IPv6 interface 2000::1234:5678 on port 890,
and on the IPv4 or IPv6 interface "an.address.net" (depending on what protocol
the resolution of this name gives) on port 546.

IPv6 addressing has a few tricky aspects, and zone indices are one of them. If
you encounter problems when configuring dpmaster for listening on a link-local
IPv6 address, I recommend that you read the paragraph called "Link-local
addresses and zone indices" on this Wikipedia page:

        http://en.wikipedia.org/wiki/IPv6_address


12) VERSION HISTORY:

    - version 2.2-dev:
        Flood protection against abusive client requests, by Timothee Besset
        New system for managing game properties (see GAME PROPERTIES above)
        Support for RtCW and WoET, using the game properties
        Shutdown heartbeats and unknown heartbeats are now ignored
        The chroot jail was preventing daemonization (fixed thanks to LordHavoc)
        The game type was incorrect when printing the server list in the log
        Less debug output when building a getserversResponse in verbose mode

    - version 2.1:
        A gametype value can now be any string, not just a number

    - version 2.0:
        Gametype filter support in the server list queries (see techinfo.txt)
        New option "--game-policy" to filter games (see GAME POLICY above)
        IPv6 support, including 2 new messages types (see techinfo.txt)
        Logging support (see LOGGING above)
        Only the last packet of a getservers response gets an EOT mark now
        The default number of servers is now 4096
        Improved listening interface option (see LISTENING INTERFACES above)
        Long format for all command line options (see SYNTAX & OPTIONS above)
        The server lists are now sent in a semi-random order, for fairness
        The new hash function supports up to 16-bit hashes
        The default hash size has been increased to 10 bits
        0 is no longer an invalid hash size
        New option "--allow-loopback", for debugging purposes only!
        New option "--hash-ports", for debugging purposes only!
        Various updates and improvements in the documentation
        No warning is printed anymore if a server changes its game name
        No longer tolerates several mapping declarations for the same address
        A lot of minor changes and fixes in the code
        The test suite now requires the Socket6 Perl module to run

    - version 1.7:
        There's now a maximum number of servers per IP address (default: 32)
        New option to set the maximum number of servers per IP address (-N)
        The maximum number of servers recorded by default is now 1024
        The default hash size has been increased from 6 bits to 8 bits
        A few Perl scripts have been added to provide basic automated testing
        A rare bug where a server was occasionally skipped was fixed
        The compilation with MS Visual Studio 2005 is fixed
        Protocol numbers less than or equal to 0 are now handled correctly
        Servers can no longer keep their slots without sending infoResponses
        Games having a name starting with a number are now handled correctly
        A few minor memory leaks were removed in the address mapping init code
        Additional checks of the command line options and the messages syntax
        The requirement of a "clients" value in infoResponses is now enforced
        The "infoResponse" description in techinfo.txt has been corrected
        The "heartbeat" description in techinfo.txt has been corrected
        The time is now printed to the console each time a packet is received
        Made it clear in the doc that any game can be supported out of the box

    - version 1.6:
        Several getserversResponse may now be sent for a single getservers
        A getserversResponse packet can no longer exceed 1400 bytes
        The maximum number of servers recorded by default has doubled (now 256)
        The default hash size has been increased from 5 bits to 6 bits
        Several updates and corrections in the documentation

    - version 1.5.1:
        Compilation on FreeBSD was fixed
        A couple of minor changes in "COMPILING DPMASTER" (in techinfo.txt)

    - version 1.5:
        Address mapping added (see ADDRESS MAPPING above)
        Servers on a loopback address are accepted again if they have a mapping
        A valid "infoReponse" is now rejected if its challenge has timed out
        The size of the challenge sent with "getinfo" has been made random
        A timed-out server is now removed as soon as a new server needs a slot
        Several little changes in the printings to make them more informative
        A technical documentation was added
        Compiling dpmaster with MSVC works again

    - version 1.4:
        Dpmaster now quits if it can't chroot, switch privileges or daemonize
        Packets coming from a loopback address are now rejected with a warning
        Listen address option added (-l)
        Modified Makefile to please BSD make

    - version 1.3.1:
        SECURITY WARNING: 2 exploitable buffer overflows were fixed
        Verbose option parsing fixed
        Paranoid buffer overflow checkings added, in case of future code changes

    - version 1.3:
        Ability to support any game which uses DP master protocol (ex: QFusion)

    - version 1.2.1:
        A major bug was fixed (a NULL pointer dereference introduced in v1.2)

    - version 1.2:
        A major bug was fixed (an infinite loop in HandleGetServers)

    - version 1.1:
        A lot of optimizations and tweakings
        Verbose option added (-v)
        Hash size option added (-H)
        Daemonization option added on UNIXes (-D)
        Chrooting and privileges dropping when running as root added on UNIXes
        MinGW cross-compilation support added

    - version 1.01:
        A major bug was fixed. Most of the servers weren't sent to the clients

    - version 1.0 :
        First publicly available version


13) CONTACTS & LINKS:

You can get the latest versions of DarkPlaces and dpmaster on the DarkPlaces
home page <http://icculus.org/twilight/darkplaces/>.

If dpmaster doesn't fit your needs, please drop me an email (my name and email
address are right below those lines): your opinion and ideas may be very
valuable to me for evolving it to a better tool.


--
Mathieu Olivier
molivier, at users.sourceforge.net
