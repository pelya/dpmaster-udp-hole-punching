
           Dpmaster, a master server for DarkPlaces, Quake III Arena
         and any game supporting the DarkPlaces master server protocol
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
a look at the technical information file for further explanations), any running
dpmaster will accept and register your game. Of course, it doesn't exempt you
from asking someone the permission to use his running instance of dpmaster. You
don't want to be rude and get banned from his dpmaster, do you?

Game engines supporting the DP master server protocol currently include
DarkPlaces and all its derived games (Nexuiz, Transfusion, ...), QFusion and
most of its derived games (Warsow, ...), and FTE QuakeWorld.

Although dpmaster is being developed primarily on a Linux machine (i386), it
should compile and run at least on any operating system from the Win32 or UNIX
family. Take a look at the "COMPILING DPMASTER" section in "techinfo.txt" for
more information. Be aware that some options are only available on UNIXes,
including all security-related options. Take a look at the "SECURITY" section
below for more information.

The source code of dpmaster is available under the GNU General Public License.


* SYNTAX & OPTIONS:

The syntax of the command line is the classic: "dpmaster [options]". Running
"dpmaster -h" will print the available options for your version.

For a simple usage, the only option you may want to know is '-D'. This is a
UNIX-only option and it makes dpmaster running as a system daemon, in the
background.


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
no privileges at all  :)  Note that this path and this user name are
customizable via the '-j' and '-u' command line options.

If you want to run dpmaster on a Win2k/WinXP system, you may want to add a
"dpmaster" user on your computer. Make it a normal user, not a power user or an
administrator. You'll then be able to run dpmaster using this low-privilege
account. Right click on "dpmaster.exe" while pressing the SHIFT button; select
"Run as...", and type "dpmaster", the password you chose for it, and your
domain main (your computer name probably). After a few seconds, dpmaster should
appear on your screen. Note that you won't be able to type anything into the
window (including closing it with Ctrl-C), you'll have to close it with the
upper right button.
For your information, Windows systems since Win2k include a command line utility
for running programs as another user, called "runas".

Finally, you will probably be glad to know that dpmaster can't leak memory: in
fact, no resources are allocated after the initialization.


* ADDRESS MAPPING:

Address mapping allows you to tell dpmaster to transmit an IP address instead
of another one to the clients, in the "getserversResponse" messages. It can be
useful in several cases. Imagine you have a dpmaster and a server behind a
firewall, with local IP addresses. You don't want the master to send the local
server IP address. Instead, you want it to send the firewall address for
instance.

Address mappings are declared on the command line, using the "-m" option. You
can declare as many of them as you want. The syntax is:

        dpmaster -m address1=address2 -m address3=address4 ...

An address can be an explicit IPv4 address, such as "192.168.1.1", or an host
name, "www.mydomain.net" for instance. Optionally, a port number can be
appended after a ':' (ex: "www.mydomain.net:1234").

The most simple mappings are host-to-host mappings. For example:

        dpmaster -m 1.2.3.4=myaddress.net

In this case, each time dpmaster would have transmitted "1.2.3.4" to a client,
it will transmit the "myaddress.net" IP address instead.

If you add a port number to the first address, then the switching will only
occurred if the server matches the address and the port number.
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


* CHANGES:

    - version 1.6.1-devel:
        The "heartbeat" description in techinfo.txt has been corrected
        The maximum number of servers recorded by default is now 1024
        The default hash size has been increased from 6 bits to 8 bits
        A rare bug where a server was occasionally skipped was fixed
        The compilation with MS Visual Studio 2005 was fixed
        Made it clearer that dpmaster can support any game out of the box

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
very valuable to me for evolving dpmaster to a better tool. Alternatively, you
may be interested by Hendrik Lipka's Q3Master. It's a master server for Q3A and
derived games such as "Return to Castle Wolfenstein". It's written in Java and
its code is available under the GNU GPL licence:
http://www.hendriklipka.de/java/q3master.html


--
Mathieu Olivier
molivier, at users.sourceforge.net
