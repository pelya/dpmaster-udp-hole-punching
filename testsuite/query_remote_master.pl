#!/usr/bin/perl -w

use strict;
use testlib;


my %defaultProtocols = (
	"Warsow" => 10,				# can also be 5307 or 5308 (Warsow 0.5t3 and 0.5t4?)
	"Quake3Arena" => 71,		# can also be 68
	
	# DarkPlaces
	"DarkPlaces-Quake" => 3,
	"Nexuiz" => 3,
);
my $defaultMasterAddr = "dpmaster.deathmask.net";


my $nbArgs = scalar @ARGV;
if ($nbArgs < 1 or $nbArgs > 3) {
	print "Syntax: $0 [options] <game> [protocol number] [master]\n";
	print "    Ex: $0 Nexuiz\n";
	print "        $0 Quake3Arena 68\n";
	print "        $0 Warsow 5308 dpmaster.deathmask.net\n";
	exit;
}

my $gamename = $ARGV[0];

my $protocol;
if ($nbArgs > 1) {
	$protocol = $ARGV[1];
}
else {
	$protocol = $defaultProtocols{$gamename};
}

my $masterAddr;
if ($nbArgs > 2) {
	$masterAddr = $ARGV[2];
}
else {
	$masterAddr = $defaultMasterAddr;
}

Master_SetProperty ("remoteAddress", $masterAddr);

my $clientRef = Client_New ();
Client_SetGameProperty ($clientRef, "gamename", $gamename);
Client_SetGameProperty ($clientRef, "protocol", $protocol);

Test_Run ("Querying $masterAddr for $gamename servers (protocol: $protocol)...", 1);
