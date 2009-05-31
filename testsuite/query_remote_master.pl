#!/usr/bin/perl -w

use strict;
use testlib;

# Libraries
use Socket;
use Socket6;


#***************************************************************************
# IsAddressIpv6
#***************************************************************************
sub IsAddressIpv6 {
	my $address = shift;
	
	my @res = getaddrinfo ($address, 0);
	if (scalar @res < 5) {
		die "Can't resolve address \"$address\"";
	}
	my $family;
	($family, @res) = @res;

	return ($family == AF_INET6);
}


my %defaultProtocols = (
	"Warsow" => 10,				# can also be 5307 or 5308 (Warsow 0.5t3 and 0.5t4)
	"Quake3Arena" => 68,		# can also be 71 (IOQuake3 1.36)
	
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

my $gamefamily;
if ($gamename eq "Quake3Arena") {
	$gamefamily = GAME_FAMILY_QUAKE3ARENA;
}
else {
	$gamefamily = GAME_FAMILY_DARKPLACES;
}
my $clientRef = Client_New ($gamefamily);
Client_SetProperty ($clientRef, "ignoreEOTMarks", 1);
Client_SetGameProperty ($clientRef, "gamename", $gamename);
Client_SetGameProperty ($clientRef, "protocol", $protocol);

if (IsAddressIpv6 ($masterAddr)) {
	Client_SetProperty ($clientRef, "useIPv6", 1);
}

Test_Run ("Querying $masterAddr for $gamename servers (protocol: $protocol)...", 1);
