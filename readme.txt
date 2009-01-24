
              Dpmaster, a master server supporting the DarkPlaces
                and Quake III Arena master server protocols
           ---------------------------------------------------------

                              General information
                              -------------------


* INTRODUCTION:

Dpmaster is a master server written from scratch for LordHavoc's great Quake 1
engine: DarkPlaces. The master protocol used by DarkPlaces and dpmaster is
heavily based on the Quake III Arena master server protocol, that's why
dpmaster supports DP servers and Q3A servers at the same time.

An interesting property of dpmaster and its associated network protocol is that
it supports any game using its protocol out of the box. As long as you send
your game name correctly in the "infoResponse" and "getservers" messages (take
a look at the "PROTOCOL" section in "doc/techinfo.txt" for further
explanations), any running dpmaster will accept and register any game. Of
course, it doesn't exempt you from asking someone the permission to use his
running instance of dpmaster. You don't want to be rude and get banned from
his master server, do you?

Game engines supporting the DP master server protocol currently include
DarkPlaces and all its derived games (such as Nexuiz and Transfusion), QFusion
and most of its derived games (such as Warsow), and FTE QuakeWorld.

Although dpmaster is being developed primarily on a Linux machine (i386), it
should compile and run at least on any operating system from the Win32 or UNIX
family. Take a look at the "COMPILING DPMASTER" section in "doc/techinfo.txt"
for more information. Be aware that some options are only available on UNIXes,
including all security-related options. Take a look at the "SECURITY" section
below for more information.

The source code of dpmaster is available under the GNU General Public License.


* SYNTAX & OPTIONS:

The syntax of the command line is the classic: "dpmaster [options]". Running
"dpmaster -h" will print the available options for your version.

All options have a long name (a string), and most of them also have a short name
(one character). In the command line, long option names are preceded by 2
hyphens and short names by 1 hyphen. For instance, you can run dpmaster as a
daemon on UNIX systems by calling either "dpmaster -D" or "dpmaster --daemon".

Most options have an associated parameter. You can either separate the option
name and its parameter by a blank space, or you can append the parameter if the
option is in its short form, or you can separate them by an equal sign if the
option is in its long form. For example, these 4 ways of running dpmaster with a
maximum number of servers of 16 are equivalent:

   * dpmaster -n 16
   * dpmaster --max-servers 16
   * dpmaster -n16
   * dpmaster --max-servers=16


* SECURITY:

First, you shouldn't be afraid to run dpmaster on your machine: at the time I
wrote those lines, only one security warning has been issued since the first
release of dpmaster. It has always been developed with security in mind and will
always be.

Also, dpmaster needs very few things to run correctly. A little bit of memory,
a few CPU cycles from time to time and a network port are its only
requirements. So feel free to restrict its privileges as much as you can.

The UNIX/Linux version of dpmaster has even a builtin security mechanism that
triggers when it is run with super-user (root) privileges. Basically, the
process locks (chroots) itself in the empty directory "/var/empty/" and drops
its privileges in favor of those of user "nobody" (notorious for having almost
no privileges at all)  :)  Note that this path and this user name are
customizable via the '-j' and '-u' command line options.

If you want to run dpmaster on a Win2k system (or better), you may want to add
a "dpmaster" user on your computer. Make it a normal user, not a power user or
an administrator. You'll then be able to run dpmaster using this low-privilege
account. Right click on "dpmaster.exe" while pressing the SHIFT button; select
"Run as...", and type "dpmaster", the password you chose for it, and your
domain main (your computer name probably). After a few seconds, dpmaster should
appear on your screen. Note that you won't be able to type anything into the
window (including closing it with Ctrl-C), you'll have to close it with the
upper right button.
For your information, Windows systems since Win2k include a command line utility
for running programs as another user, called "runas".

Finally, you will probably be glad to know that dpmaster can't leak memory: in
fact, no resources are allocated after the initialization phase.


* LOGGING:

You can enable logging by adding "-L" or "--log" to the command line. The
default name of the log file is "dpmaster.log", either in the working directory
for Windows systems, or in the "/var/log" directory for UNIX systems. You can
change the path and name of this file using the "--log-file" option.

The obvious way to use the log is to enable it by default. But if you want to do
that, you may consider using a lesser verbose level ("-v" or "--verbose", with a
value of 1 - only errors, or 2 - only errors and warnings), as dpmaster tends
to be pretty verbose.

Another way to use the log is to set maximum verbose output, but to enable it
only when needed, and then to disable it afterwards. This is possible on the
systems that provide POSIX signals USR1 and USR2, which means all supported
systems except the Windows family. When dpmaster receives the USR1 signal, it
opens its log file (or reopens it if it was already opened), dumps the list of
all registered servers, and then proceeds with its normal logging. When it
receives the USR2 signal, it closes its log file.

Note that dpmaster will never overwrite an existing log file, it always appends
logs to it. It prevents you from losing a potentially important log by mistake,
with the drawback of having to clean the logs by hand.

A couple of final remarks regarding the log file names : first, if you run
dpmaster as a daemon, remember that its working directory is the root directory,
so be careful with absolute paths. And second, if you put your dpmaster into a
chroot jail, don't forget when starting or restart the log that its path will
then be relative to the jail root directory. Watch out for the log directory not
being created or having wrong permissions in this case.


* ADDRESS MAPPING:

Address mapping allows you to tell dpmaster to transmit an IPv4 address instead
of another one to the clients, in the "getserversResponse" messages. It can be
useful in several cases. Imagine you have a dpmaster and a server behind a
firewall, with local IPv4 addresses. You don't want the master to send the local
server IP address. Instead, you want it to send the firewall address for
instance.

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
it's actually the only way to make dpmaster accept a server talking from a
loopback address.


* LISTENING INTERFACES:

By default, dpmaster creates one IPv4 socket and one IPv6 socket (if IPv6
support is available of course). It will listen on every network interface, on
the default port unless you specified another one using "-p" or "--port". If
you want it to listen on one or more particular interface(s) instead, you will
have to use the command line option "-l" or "--listen".

Running dpmaster with no "-l" option is (almost) like running it with:

    dpmaster --listen 0.0.0.0 --listen ::

The first option is for listening on all IPv4 interfaces, the second for
listening on all IPv6 interfaces, both on the default port. The only
difference with having no "-l" is that dpmaster will abort in this case if IPv4
or IPv6 isn't supported by your system, as you have explicitely requested those
network sockets to be opened. Note that if you don't want dpmaster to listen
on IPv6 interfaces, you can easily do it by only specifying "-l 0.0.0.0" on
the command line.

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
IPv6 address, I recommend that you read the following paragraph on Wikipedia,
to make sure you use the correct syntax for your link-local address:
http://en.wikipedia.org/wiki/IPv6#Address_scopes_and_zone_indices


* CHANGES:

    - version 2.0-devel:
        IPv6 support, including 2 new messages types (see techinfo.txt)
        Logging support (see LOGGING above)
        The default number of servers is now 4096
        Improved listening interface option (see LISTENING INTERFACES above)
        Long format for all command line options (see SYNTAX & OPTIONS above)
        The new hash function supports up to 16-bit hashes
        0 is no longer an invalid hash size
        New option "--allow-loopback", for debugging purposes only!
        New option "--hash-ports", for debugging purposes only!
        Various updates and improvements in the documentation
        The test suite now requires the Socket6 Perl module

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


* CONTACTS & LINKS:

You can get more informations and the latest versions of DarkPlaces and
dpmaster on the DarkPlaces home page: http://icculus.org/twilight/darkplaces/

If dpmaster doesn't fit you needs, please drop me an email. Your opinion may be
very valuable to me for evolving dpmaster to a better tool.


--
Mathieu Olivier
molivier, at users.sourceforge.net
