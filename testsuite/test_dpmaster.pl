#!/usr/bin/perl -w

#***************************************************************************
#                              test_dpmaster.pl
#          A Perl script for testing the DarkPlaces master server
#***************************************************************************

use strict;

# Libraries
use Socket;
use Fcntl;


# Constants
use constant NUMBER_VIRTUAL_SERVERS => 2;

use constant HEARTBEAT_START_INTERVAL => 2;  # in seconds
use constant HEARTBEAT_INTERVAL => 10;  # in seconds
use constant TEST_TIME => 5;  # in seconds

use constant DEFAULT_MASTER_PORT => 27950;
use constant MAPPED_ADDRESS => "172.16.12.34";
use constant DPMASTER_OPTIONS => "-v -m 127.0.0.1=" . MAPPED_ADDRESS;


# Global variables
my @serverList = ();
my $mustExit = 0;
my $currentTime = 0;
my $testTime = 0;
my $dpmasterPid = undef;


#***************************************************************************
# PrintSyntaxAndDie
#***************************************************************************
sub PrintSyntaxAndDie {
	print "Syntax: $0 <dpmaster path>\n";
	exit -1;
}

#***************************************************************************
# StartVirtualServer
#***************************************************************************
sub StartVirtualServer {
	my $gamename = shift;
	my $protocol = shift;
	my $port = shift;
	my $maxclients = shift;
	my $nbclients = shift;

	my $socket;
	socket ($socket, PF_INET, SOCK_DGRAM, getprotobyname("udp")) or die "Can't create socket: $!\n";
	my $addr = sockaddr_in ($port, INADDR_ANY);
	bind ($socket, $addr) or die "Can't bind to port $port: $!\n";

	# Connect the socket to the dpmaster address
	my $dpmasterAddr = sockaddr_in (DEFAULT_MASTER_PORT, INADDR_LOOPBACK);
	connect ($socket, $dpmasterAddr) or die "Can't connect to the dpmaster address: $!\n";

	# Make the IOs from this socket non-blocking
    my $flags = fcntl ($socket, F_GETFL, 0) or die "Can't get flags for the socket: $!\n";
    fcntl ($socket, F_SETFL, $flags | O_NONBLOCK) or die "Can't set the socket as non-blocking: $!\n";

	my $nextHeartbeat = $currentTime + HEARTBEAT_START_INTERVAL;
	my $index = $#serverList + 1;

	push @serverList, {
		index => $index,
		gamename => $gamename,
		protocol => $protocol,
		maxclients => $maxclients,
		nbclients => $nbclients,
		port => $port,
		nextHeartbeat => $nextHeartbeat,
		socket => $socket
	};
	
	print "Virtual server $index started on port $port\n";
}

#***************************************************************************
# SignalHandler
#***************************************************************************
sub SignalHandler {
	# If it's the second time we get a signal during this frame, just exit
	if ($mustExit) {
		die "Double signal received\n";
	}
	$mustExit = 1;

	my $signal = shift;
	print "Signal $signal received. Exiting...\n";
}

#***************************************************************************
# SendHeartbeat
#***************************************************************************
sub SendHeartbeat {
	my $server = shift;
	
	print "Sending heartbeat from server $server->{index}\n";
	my $heartbeat = "\xFF\xFF\xFF\xFFheartbeat DarkPlaces\x0A";
	send ($server->{socket}, $heartbeat, 0) or die "Can't send packet: $!";
	$server->{nextHeartbeat} = $currentTime + HEARTBEAT_INTERVAL;
}

#***************************************************************************
# SendInfoResponse
#***************************************************************************
sub SendInfoResponse {
	my $server = shift;
	my $challenge = shift;

	print "Sending infoResponse from server $server->{index}\n";
	my $infoResponse = "\xFF\xFF\xFF\xFFinfoResponse\x0A" . 
						"\\protocol\\$server->{protocol}" .
						"\\gamename\\$server->{gamename}" .
						"\\sv_maxclients\\$server->{maxclients}" .
						"\\clients\\$server->{nbclients}" .
						"\\challenge\\$challenge";
	send ($server->{socket}, $infoResponse, 0) or die "Can't send packet: $!";
}

#***************************************************************************
# SendInfoResponse
#***************************************************************************
sub SendGetServers {
	my $client = shift;
	
	print "Sending getservers from client $client->{index}\n";
	my $getservers = "\xFF\xFF\xFF\xFFgetservers $client->{gamename} $client->{protocol} empty full";
	send ($client->{socket}, $getservers, 0) or die "Can't send packet: $!";
}

#***************************************************************************
# HandleMessage
#***************************************************************************
sub HandleMessage {
	my $server = shift;
	my $message = shift;
	
	unless ($message =~ s/^\xFF\xFF\xFF\xFF(.*)/$1/) {
		print "Invalid message\n";
		return;
	}
	
	if ($message =~ /^getinfo +(\S+)$/) {
		my $challenge = $1;
		print "Server $server->{index} received a getinfo with challenge \"$challenge\"\n";
		SendInfoResponse ($server, $challenge);
	}
	elsif ($message =~ /^getserversResponse(\\.*)/) {
		use bytes;

		my $addrList = $1;
		my $strlen = length($addrList);
		print "Server $server->{index} received a getserversResponse (" . ($strlen / 7 - 1) . " servers listed)\n";
		
		for (;;) {
			unless ($addrList =~ s/\\(.{4})(.{2})(\\.*|)$/$3/) {
				print "* Unexpected end of list\n";
				last;
			}
			my $address = $1;
			my $port = unpack ("n", $2);

			if ($address eq "EOT\0" and $port == 0) {
				print "* End of transmission found\n";
				last;
			}

			print "* Found a server at address " . inet_ntoa($address) . " on port " . $port . "\n";
		}
	}
	else {
		print "WARNING: server $server->{index} received a message of an unknow type (text: $message)\n";
	}

	# TODO ...
}

#***************************************************************************
# SimulateServer
#***************************************************************************
sub SimulateServer {
	my $server = shift;
	
	# If it's time to send an heartbeat
	if ($server->{nextHeartbeat} <= $currentTime) {
		SendHeartbeat ($server);
	}
	
	# Get and handle the network packets
	my $recvPacket;
	if (recv ($server->{socket}, $recvPacket, 512, 0)) {
		HandleMessage ($server, $recvPacket);
	}
}

#***************************************************************************
# StopVirtualServers
#***************************************************************************
sub StopVirtualServers {
	foreach my $server (@serverList) {
		close ($server->{socket});
	}
}

#***************************************************************************
# RunSimulation
#***************************************************************************
sub RunSimulation {
	for (;;) {
		$currentTime = time;

		# Simulate each server
		foreach my $server (@serverList) {
			SimulateServer ($server);
		}
		
		# Print the master server output
		if (defined $dpmasterPid) {
			while (<DPMASTER>) {
				print "DPM >>> $_";
			}
		}

		# If it's time to do the test
		if ($testTime >0 and $currentTime >= $testTime) {
			# Request from the first server the list of all servers registered on the master
			SendGetServers ($serverList[0]);
			$testTime = 0;
		}

		# Look for inconsistencies
		# ... TODO ...

		# Sleep a bit to avoid wasting the CPU time
		# TODO: sleep half a second or a quarter of a second would be better. Or do a select()
		# on the dpmaster handle + all the sockets, with a timeout (for heartbeat among other things)
		sleep (1) unless ($mustExit);

		# Check exit conditions
		last if ($mustExit);
	}
}

#***************************************************************************
# Main function
#***************************************************************************
$currentTime = time;
$testTime = $currentTime + TEST_TIME;

$SIG{TERM} = $SIG{HUP} = $SIG{INT} = \&SignalHandler;

if (scalar @ARGV > 1) {
	PrintSyntaxAndDie();
}

# Run DPMaster
if (scalar @ARGV > 0) {
	my $dpmasterPath = $ARGV[0];
	my $dpmasterCmdLine = "$dpmasterPath " . DPMASTER_OPTIONS;
	print "Launching dpmaster as: $dpmasterCmdLine\n";
	$dpmasterPid = open DPMASTER, "$dpmasterCmdLine |";
	if (not defined $dpmasterPid) {
		die "Can't run dpmaster: $!\n";
	}

	# Make the IOs from dpmaster's pipe non-blocking
    my $flags = fcntl (DPMASTER, F_GETFL, 0) or die "Can't get flags for the dpmaster pipe: $!\n";
    fcntl (DPMASTER, F_SETFL, $flags | O_NONBLOCK) or die "Can't set the dpmaster pipe as non-blocking: $!\n";
}

# Initialize the virtual servers
my $svIndex;
for ($svIndex = 0; $svIndex < NUMBER_VIRTUAL_SERVERS; ++$svIndex) {
	StartVirtualServer ("TestDPMaster", 5, 4322 + $svIndex, 8, $svIndex);
}

RunSimulation ();

# Cleanup
StopVirtualServers ();
if (defined $dpmasterPid) {
	close DPMASTER;
}

print "Success!\n";
