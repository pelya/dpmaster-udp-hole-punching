#!/usr/bin/perl -w

use strict;
use testlib;


my $gamename = "Warsow";
my $protocol = 10;
# "Warsow": 10
# "Quake3Arena": 68 or 71
# "Nexuiz" and "DarkPlaces-Quake" (i.e. DarkPlaces): 3

my $masterAddr = "dpmaster.deathmask.net";


Master_SetProperty ("remoteAddress", $masterAddr);

my $clientRef = Client_New ();
Client_SetGameProperty ($clientRef, "gamename", $gamename);
Client_SetGameProperty ($clientRef, "protocol", $protocol);

Test_Run ("Querying $masterAddr for $gamename servers (protocol: $protocol)...", 1);

