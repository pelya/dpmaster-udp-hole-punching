package testlib;

use strict;
use warnings;

# Libraries
use Fcntl;
use Getopt::Long;
use POSIX qw(:sys_wait_h :stdlib_h);
use Socket;
use Socket6;
use Time::HiRes qw(time sleep);


# Constants - dpmaster
use constant DEFAULT_DPMASTER_PATH => "../src/dpmaster";
use constant DEFAULT_DPMASTER_OPTIONS => "--allow-loopback --hash-ports";
use constant IPV4_ADDRESS => "127.0.0.1";
use constant IPV6_ADDRESS => "::1";
use constant DEFAULT_DPMASTER_PORT => 27950;

# Constants - game properties
use constant DEFAULT_GAMENAME => "DpmasterTest";
use constant DEFAULT_PROTOCOL => 5;
use constant QUAKE3ARENA_PROTOCOL => 67;

# Constants - misc
use constant DEFAULT_SERVER_PORT => 5678;
use constant DEFAULT_CLIENT_PORT => 4321;
use constant {
	GAME_FAMILY_DARKPLACES => 0,
	GAME_FAMILY_QUAKE3ARENA => 1,
};


# Global variables - dpmaster
my $dpmasterPid = undef;
my %dpmasterProperties = (
	cmdlineoptions => DEFAULT_DPMASTER_OPTIONS,
	port => DEFAULT_DPMASTER_PORT,
	exitvalue => undef
);

# Global variables - servers
my @serverList = ();
my $nextServerPort = DEFAULT_SERVER_PORT;
my $nextServerId = 0;

# Global variables - clients
my @clientList = ();
my $nextClientPort = DEFAULT_CLIENT_PORT;
my $nextClientId = 0;

# Global variables - misc
my $currentTime = time;
my $mustExit = 0;
my $testNumber = 0;
my @failureDiagnostic = ();

# Command-line options
my $optVerbose = 0;
my $optDpmasterOutput = 0;


#***************************************************************************
# BEGIN block
#***************************************************************************
BEGIN {
	use Exporter ();

	our ($VERSION, @ISA, @EXPORT);
	$VERSION = v1.0;
	@ISA = qw(Exporter);
	@EXPORT = qw(
		&Client_New
		&Client_SetGameProperty
		&Client_SetProperty

		&Master_GetProperty
		&Master_SetProperty

		&Server_GetGameProperty
		&Server_New
		&Server_SetGameProperty
		&Server_SetProperty

		&Test_Run

		GAME_FAMILY_DARKPLACES
		GAME_FAMILY_QUAKE3ARENA
	);
}


#***************************************************************************
# INIT block
#***************************************************************************
INIT {
	# Parse the options
	GetOptions (
		"verbose" => \$optVerbose,
		"dpmaster-output" => \$optDpmasterOutput,
	);

	# Install the signal handler
	$SIG{TERM} = $SIG{HUP} = $SIG{INT} = \&Test_SignalHandler;
}


#***************************************************************************
# END block
#***************************************************************************
END {
	Test_StopAll ();
}


#***************************************************************************
# Common_CreateSocket
#***************************************************************************
sub Common_CreateSocket {
	my $port = shift;
	my $useIPv6 = shift;

	my $proto = getprotobyname("udp");
	my ($family, $addr, $dpmasterAddr);
	if ($useIPv6) {
		# Build the address for connect()
		my @res = getaddrinfo (IPV6_ADDRESS, $dpmasterProperties{port}, AF_INET6, SOCK_DGRAM, $proto, AI_NUMERICHOST);
		if (scalar @res < 5) {
			die "Can't resolve address [" . IPV6_ADDRESS . "]:$port";
		}
		my ($sockType, $canonName);
        ($family, $sockType, $proto, $dpmasterAddr, $canonName, @res) = @res;
		
		# Build the address for bind()
		@res = getaddrinfo (IPV6_ADDRESS, $port, AF_INET6, SOCK_DGRAM, $proto, AI_NUMERICHOST | AI_PASSIVE);
		if (scalar @res < 5) {
			die "Can't resolve address [" . IPV6_ADDRESS . "]:$port";
		}
        ($family, $sockType, $proto, $addr, $canonName, @res) = @res;
	}
	else {
		$family = PF_INET;
		$addr = sockaddr_in ($port, INADDR_LOOPBACK);
		$dpmasterAddr = sockaddr_in ($dpmasterProperties{port}, INADDR_LOOPBACK);
	}

	# Open an UDP socket
	my $socket;
	socket ($socket, $family, SOCK_DGRAM, $proto) or die "Can't create socket: $!\n";

	# Bind it to the port
	bind ($socket, $addr) or die "Can't bind to port $port: $!\n";

	# Connect the socket to the dpmaster address
	connect ($socket, $dpmasterAddr) or die "Can't connect to the dpmaster address: $!\n";

	# Make the IOs from this socket non-blocking
	Common_SetNonBlockingIO($socket);
	
	return $socket;
}


#***************************************************************************
# Common_VerbosePrint
#***************************************************************************
sub Common_VerbosePrint {

	if ($optVerbose) {
		my $string = shift;

		print ("        " . $string);
	}
}


#***************************************************************************
# Common_SetNonBlockingIO
#***************************************************************************
sub Common_SetNonBlockingIO {
	my $handle = shift;

    my $flags = fcntl ($handle, F_GETFL, 0) or die "Can't get the handle's flags: $!\n";
    fcntl ($handle, F_SETFL, $flags | O_NONBLOCK) or die "Can't set the handle as non-blocking: $!\n";
}


#***************************************************************************
# Client_CheckServerList
#***************************************************************************
sub Client_CheckServerList {
	my $clientRef = shift;

	my $clUseIPv6 = $clientRef->{useIPv6};
	my $clPropertiesRef = $clientRef->{gameProperties};
	my $clGamename = $clPropertiesRef->{gamename};
	my $clProtocol = $clPropertiesRef->{protocol};

	my $returnValue = 1;

	my @clientServerList = @{$clientRef->{serverList}};
	foreach my $serverRef (@serverList) {
		my $svUseIPv6 = $serverRef->{useIPv6};
		my $svPropertiesRef = $serverRef->{gameProperties};
		my $svGamename = $svPropertiesRef->{gamename};
		my $svProtocol = $svPropertiesRef->{protocol};
		
		# Skip this server if it doesn't match the conditions
		if (($svUseIPv6 != $clUseIPv6) or
			($svProtocol != $clProtocol) or
			(defined ($svGamename) != defined ($clGamename)) or
			(defined ($svGamename) and ($svGamename ne $clGamename))) {
			next;
		}

		# Skip this server if it shouldn't be registered
		next if ($serverRef->{cannotBeRegistered});

		my $fullAddress = ($svUseIPv6 ? "[" . IPV6_ADDRESS . "]" : IPV4_ADDRESS);
		$fullAddress .= ":" . $serverRef->{port};
		
		my $found = 0;
		foreach my $index (0 .. $#clientServerList) {
			if ($clientServerList[$index] eq $fullAddress) {
				$found = 1;
				Common_VerbosePrint ("CheckServerList: found server $fullAddress\n");
				splice (@clientServerList, $index, 1);
				last;
			}
		}
		if (not $found) {
			push @failureDiagnostic, "CheckServerList: server $fullAddress missed by client $clientRef->{id}";
			$returnValue = 0;
		}
	}
	
	# If there is unknown servers in the list
	if (scalar @clientServerList > 0) {
		foreach my $unknownServer (@clientServerList) {
			push @failureDiagnostic, "CheckServerList: server $unknownServer erroneously sent to client $clientRef->{id}";
		}
		$returnValue = 0;
	}

	return $returnValue;
}


#***************************************************************************
# Client_HandleGetServersReponse
#***************************************************************************
sub Client_HandleGetServersReponse {
	use bytes;

	my $clientRef = shift;
	my $addrList = shift;
	my $extended = shift;

	my $strlen = length($addrList);
	Common_VerbosePrint ("Client received a getservers" . ($extended ? "Ext" : "") . "Response\n");
	
	for (;;) {
		if ($addrList =~ s/^\\(.{4})(.{2})([\\\/].*|)$/$3/) {
			my $address = $1;
			my $port = unpack ("n", $2);

			# If end of transmission is found
			if ($address eq "EOT\0" and $port == 0) {
				Common_VerbosePrint ("    * End Of Transmission\n");
				last;
			}

			my $fullAddress = inet_ntoa ($address) . ":" . $port;
			push @{$clientRef->{serverList}}, $fullAddress;
			Common_VerbosePrint ("    * Found a server at $fullAddress\n");
		}
		elsif ($addrList =~ s/^\/(.{16})(.{2})([\\\/].*|)$/$3/) {
			my $address = $1;
			my $port = unpack ("n", $2);

			my $fullAddress = "[" . inet_ntop (AF_INET6, $address) . "]:" . $port;
			push @{$clientRef->{serverList}}, $fullAddress;
			Common_VerbosePrint ("    * Found a server at $fullAddress\n");
		}
		else {
			Common_VerbosePrint ("    * WARNING: unexpected end of list\n");
			last;
		}
	}
	
	$clientRef->{serverListCount}++;
}


#***************************************************************************
# Client_New
#***************************************************************************
sub Client_New {
	my $gameFamily = shift;

	if (not defined ($gameFamily)) {
		$gameFamily = GAME_FAMILY_DARKPLACES;
	}

	# Pick a port number
	my $port = $nextClientPort;
	$nextClientPort++;

	# Pick an ID
	my $id = $nextClientId;
	$nextClientId++;

	# Game family specific variables
	my ($gamename, $protocol);
	if ($gameFamily == GAME_FAMILY_QUAKE3ARENA) {
		$gamename = undef;
		$protocol = QUAKE3ARENA_PROTOCOL;
	}
	else {  # $gameFamily == GAME_FAMILY_DARKPLACES
		$gamename = DEFAULT_GAMENAME;
		$protocol = DEFAULT_PROTOCOL;
	}

	my $newClient = {
		id => $id,
		state => undef,  # undef -> Init -> WaitingServerList -> Done
		port => $port,
		socket => undef,
		serverList => [],
		serverListCount => 0,  # Nb of server lists received
		alwaysUseExtendedQuery => 0,
		useIPv6 => 0,

		gameProperties => {
			gamename => $gamename,
			protocol => $protocol
		}
	};
	push @clientList, $newClient;

	return $newClient;
}


#***************************************************************************
# Client_Run
#***************************************************************************
sub Client_Run {
	my $clientRef = shift;

	my $state = $clientRef->{state};

	# "Init" state
	if ($state eq "Init") {
		# Wait for all the servers to be registered to ask for the server list
		my $allServersRegistered = 1;
		foreach my $serverRef (@serverList) {
			if ($serverRef->{state} ne "Done" and
				not $serverRef->{cannotBeRegistered}) {
				$allServersRegistered = 0;
				last;
			}
		}

		if ($allServersRegistered) {
			Client_SendGetServers ($clientRef);
			$clientRef->{state} = "WaitingServerList";
		}
	}

	# "WaitingServerList" state
	elsif ($state eq "WaitingServerList") {
		my $recvPacket;
		if (recv ($clientRef->{socket}, $recvPacket, 1500, 0)) {
			# If we received a server list, unpack it
			if ($recvPacket =~ /^\xFF\xFF\xFF\xFFgetservers(Ext)?Response([\\\/].*)$/) {
				my $extended = ((defined $1) and ($1 eq "Ext"));
				my $addrList = $2;

				Client_HandleGetServersReponse($clientRef, $addrList, $extended);

				# FIXME: handle the case when the master sends the list in more than 1 packet
				$clientRef->{state} = "Done";
			}
			else {
				# FIXME: report the error correctly instead of just dying
				die "Invalid message received while waiting for the server list";
			}
		}
	}

	# "Done" state
	elsif ($state eq "Done") {
		# Nothing to do
	}

	# Invalid state
	else {
		die "Invalid client state: $state";
	}
}


#***************************************************************************
# Client_SendGetServers
#***************************************************************************
sub Client_SendGetServers {
	my $clientRef = shift;

	Common_VerbosePrint ("Sending getservers from client\n");
	my $gameProp = $clientRef->{gameProperties};

	my $getservers = "\xFF\xFF\xFF\xFFgetservers";
	if ($clientRef->{useIPv6} or $clientRef->{alwaysUseExtendedQuery}) {
		$getservers .= "Ext";
	}

	if (defined ($gameProp->{gamename})) {
		$getservers .= " $gameProp->{gamename}";
	}
	$getservers .= " $gameProp->{protocol} empty full";
	send ($clientRef->{socket}, $getservers, 0) or die "Can't send packet: $!";
}


#***************************************************************************
# Client_SetGameProperty
#***************************************************************************
sub Client_SetGameProperty {
	my $clientRef = shift;
	my $propertyName = shift;
	my $propertyValue = shift;
	
	$clientRef->{gameProperties}{$propertyName} = $propertyValue;
}

	
#***************************************************************************
# Client_SetProperty
#***************************************************************************
sub Client_SetProperty {
	my $clientRef = shift;
	my $propertyName = shift;
	my $propertyValue = shift;
	
	# If the property doesn't exist, there is a problem in the caller script
	die if (not exists $clientRef->{$propertyName});

	$clientRef->{$propertyName} = $propertyValue;
}

	
#***************************************************************************
# Client_Start
#***************************************************************************
sub Client_Start {
	my $clientRef = shift;

	$clientRef->{socket} = Common_CreateSocket ($clientRef->{port}, $clientRef->{useIPv6});
	$clientRef->{state} = "Init";
}

	
#***************************************************************************
# Client_Stop
#***************************************************************************
sub Client_Stop {
	my $clientRef = shift;

	my $socket = $clientRef->{socket};
	if (defined ($socket)) {
		close ($socket);
		$clientRef->{socket} = undef;
	}

	# Clean the server list
	$clientRef->{serverList} = [];
	$clientRef->{serverListCount} = 0;
}

	
#***************************************************************************
# Master_Run
#***************************************************************************
sub Master_Run {
	# Print the master server output
	while (<DPMASTER_PROCESS>) {
		if ($optDpmasterOutput) {
			Common_VerbosePrint ("[DPM] $_");
		}
	}
}

	
#***************************************************************************
# Master_GetProperty
#***************************************************************************
sub Master_GetProperty {
	my $propertyName = shift;
	
	return $dpmasterProperties{$propertyName};
}

	
#***************************************************************************
# Master_SetProperty
#***************************************************************************
sub Master_SetProperty {
	my $propertyName = shift;
	my $propertyValue = shift;
	
	$dpmasterProperties{$propertyName} = $propertyValue;
}


#***************************************************************************
# Master_Start
#***************************************************************************
sub Master_Start {
	my $dpmasterCmdLine;
	if (scalar @ARGV > 0) {
		$dpmasterCmdLine = $ARGV[0];
	}
	else {
		$dpmasterCmdLine = DEFAULT_DPMASTER_PATH;
	}

	if ($optDpmasterOutput) {
		$dpmasterCmdLine .= " -v";
	}
	
	my $additionalOptions = $dpmasterProperties{cmdlineoptions};
	if (defined ($additionalOptions) and $additionalOptions ne "") {
		$dpmasterCmdLine .= " " . $additionalOptions;
	}

	Common_VerbosePrint ("Launching dpmaster as: $dpmasterCmdLine\n");
	$dpmasterPid = open DPMASTER_PROCESS, "$dpmasterCmdLine |";
	if (not defined $dpmasterPid) {
	   die "Can't run dpmaster: $!\n";
	}

	# Make the IOs from dpmaster's pipe non-blocking
	Common_SetNonBlockingIO(\*DPMASTER_PROCESS);
}


#***************************************************************************
# Master_Stop
#***************************************************************************
sub Master_Stop {
	# Kill dpmaster if it's still running
	if (defined ($dpmasterPid)) {
		kill ("HUP", $dpmasterPid);
		$dpmasterPid = undef;
	}

	# Close the pipe
	close (DPMASTER_PROCESS);
}

	
#***************************************************************************
# Server_GetGameProperty
#***************************************************************************
sub Server_GetGameProperty {
	my $serverRef = shift;
	my $propertyName = shift;
	
	return $serverRef->{gameProperties}{$propertyName};
}


#***************************************************************************
# Server_New
#***************************************************************************
sub Server_New {
	my $gameFamily = shift;

	if (not defined ($gameFamily)) {
		$gameFamily = GAME_FAMILY_DARKPLACES;
	}

	# Pick a port number
	my $port = $nextServerPort;
	$nextServerPort++;

	# Pick an ID
	my $id = $nextServerId;
	$nextServerId++;


	# Game family specific variables
	my ($gamename, $protocol, $masterProtocol);
	if ($gameFamily == GAME_FAMILY_QUAKE3ARENA) {
		$gamename = undef;
		$protocol = QUAKE3ARENA_PROTOCOL;
		$masterProtocol = "QuakeArena-1";
	}
	else {  # $gameFamily == GAME_FAMILY_DARKPLACES
		$gamename = DEFAULT_GAMENAME;
		$protocol = DEFAULT_PROTOCOL;
		$masterProtocol = "DarkPlaces";
	}

	my $newServer = {
		id => $id,
		state => undef,  # undef -> Init -> WaitingGetInfos -> Done
		heartbeatTime => undef,
		port => $port,
		masterProtocol => $masterProtocol,
		socket => undef,
		cannotBeRegistered => 0,
		useIPv6 => 0,
		
		gameProperties => {
			gamename => $gamename,
			protocol => $protocol,
			sv_maxclients => 8,
			clients => 2
		}
	};
	push @serverList, $newServer;

	return $newServer;
}

	
#***************************************************************************
# Server_Run
#***************************************************************************
sub Server_Run {
	my $serverRef = shift;

	my $state = $serverRef->{state};

	# "Init" state
	if ($state eq "Init") {
		# If it's time to send an heartbeat
		if ($currentTime >= $serverRef->{heartbeatTime}) {
			Server_SendHeartbeat ($serverRef);

			$serverRef->{heartbeatTime} = undef;
			$serverRef->{state} = "WaitingGetInfos";
		}
	}

	# "WaitingGetInfos" state
	elsif ($state eq "WaitingGetInfos") {
		my $recvPacket;
		if (recv ($serverRef->{socket}, $recvPacket, 1500, 0)) {
			# If we received a getinfo message, reply to it
			if ($recvPacket =~ /^\xFF\xFF\xFF\xFFgetinfo +(\S+)$/) {
				my $challenge = $1;
				Common_VerbosePrint ("Server $serverRef->{id} received a getinfo with challenge \"$challenge\"\n");
				Server_SendInfoResponse ($serverRef, $challenge);
				$serverRef->{state} = "Done";
			}
			else {
				# FIXME: report the error correctly instead of just dying
				die "Invalid message received while waiting for getinfos";
			}
		}
	}

	# "Done" state
	elsif ($state eq "Done") {
		# Nothing to do
	}

	# Invalid state
	else {
		die "Invalid server state: $state";
	}
}

	
#***************************************************************************
# Server_SendHeartbeat
#***************************************************************************
sub Server_SendHeartbeat {
	my $serverRef = shift;

	Common_VerbosePrint ("Sending heartbeat from server $serverRef->{id}\n");
	my $heartbeat = "\xFF\xFF\xFF\xFFheartbeat $serverRef->{masterProtocol}\x0A";
	send ($serverRef->{socket}, $heartbeat, 0) or die "Can't send packet: $!";
}

	
#***************************************************************************
# Server_SendInfoResponse
#***************************************************************************
sub Server_SendInfoResponse {
	my $serverRef = shift;
	my $challenge = shift;

	Common_VerbosePrint ("Sending infoResponse from server $serverRef->{id}\n");
	my $infoResponse = "\xFF\xFF\xFF\xFFinfoResponse\x0A" . 
						"\\challenge\\$challenge";

	# Append all game properties to the message
	while (my ($propKey, $propValue) = each %{$serverRef->{gameProperties}}) {
		if (defined ($propValue)) {
			$infoResponse .= "\\$propKey\\$propValue";
		}
	}

	send ($serverRef->{socket}, $infoResponse, 0) or die "Can't send packet: $!";
}

	
#***************************************************************************
# Server_SetGameProperty
#***************************************************************************
sub Server_SetGameProperty {
	my $serverRef = shift;
	my $propertyName = shift;
	my $propertyValue = shift;
	
	$serverRef->{gameProperties}{$propertyName} = $propertyValue;
}

	
#***************************************************************************
# Server_SetProperty
#***************************************************************************
sub Server_SetProperty {
	my $serverRef = shift;
	my $propertyName = shift;
	my $propertyValue = shift;
	
	# If the property doesn't exist, there is a problem in the caller script
	die if (not exists $serverRef->{$propertyName});

	$serverRef->{$propertyName} = $propertyValue;
}

	
#***************************************************************************
# Server_Start
#***************************************************************************
sub Server_Start {
	my $serverRef = shift;

	$serverRef->{socket} = Common_CreateSocket($serverRef->{port}, $serverRef->{useIPv6});
	$serverRef->{state} = "Init";
	$serverRef->{heartbeatTime} = $currentTime + 0.5;
}

	
#***************************************************************************
# Server_Stop
#***************************************************************************
sub Server_Stop {
	my $serverRef = shift;

	my $socket = $serverRef->{socket};
	if (defined ($socket)) {
		close ($socket);
		$serverRef->{socket} = undef;
	}
}


#***************************************************************************
# Test_RunAll
#***************************************************************************
sub Test_RunAll {
	Master_Run ();

	foreach my $server (@serverList) {
		Server_Run ($server);
	}

	foreach my $client (@clientList) {
		Client_Run ($client);
	}
}


#***************************************************************************
# Test_SignalHandler
#***************************************************************************
sub Test_SignalHandler {
	# If it's the second time we get a signal during this frame, just exit
	if ($mustExit) {
		die "Double signal received\n";
	}
	$mustExit = 1;

	my $signal = shift;
	Common_VerbosePrint ("Signal $signal received. Exiting...\n");
}


#***************************************************************************
# Test_StartAll
#***************************************************************************
sub Test_StartAll {
	Master_Start ();

	foreach my $server (@serverList) {
		Server_Start ($server);
	}

	foreach my $client (@clientList) {
		Client_Start ($client);
	}
}


#***************************************************************************
# Test_StopAll
#***************************************************************************
sub Test_StopAll {
	foreach my $client (@clientList) {
		Client_Stop ($client);
	}

	foreach my $server (@serverList) {
		Server_Stop ($server);
	}

	Master_Stop ();
}


#***************************************************************************
# Test_Run
#***************************************************************************
sub Test_Run {
	my $testTitle = shift;
	
	$testNumber++;
	if (not defined ($testTitle)) {
		$testTitle = "Test " . $testNumber;
	}
	print ("    * " . $testTitle . "\n");

	@failureDiagnostic = ();
	$currentTime = time();

	Test_StartAll ();

	my $testTime = $currentTime + 3;  # 3 sec of test
	my $exitValue = undef;

	for (;;) {
		$currentTime = time;

		# Check exit conditions
		last if ($mustExit or $currentTime >= $testTime);

		Test_RunAll ();

		# If the dpmaster process is dead
		if (waitpid($dpmasterPid, WNOHANG) == $dpmasterPid) {
			$exitValue = $? >> 8;
			my $receivedSignal = $? & 127;
			Common_VerbosePrint ("Dpmaster end status: $? (exit value = $exitValue, received signal = $receivedSignal)...\n");
			last;
		}

		# Sleep a bit to avoid wasting the CPU time
		sleep (0.1);
	}

	my $Result = EXIT_SUCCESS;

	# If the dpmaster process is in the expected state
	my $expectedExitValue = $dpmasterProperties{exitvalue};
	if ((defined ($expectedExitValue) != defined ($exitValue)) or
		(defined ($expectedExitValue) and defined ($exitValue) and $expectedExitValue != $exitValue)) {
		$Result = EXIT_FAILURE;
		
		my $state;
		if (defined ($exitValue)) {
			$state = "dead";
		}
		else
		{
			$state = "running";
		}
		push @failureDiagnostic, "The dpmaster process is in an unexpected state ($state)";
	}

	# Check that the server lists we got are valid
	if ($Result == EXIT_SUCCESS) {
		foreach my $client (@clientList) {
			if (not Client_CheckServerList ($client)) {
				$Result = EXIT_FAILURE;
				last;
			}
		}
	}

	# TODO: any other tests?

	if ($Result == EXIT_SUCCESS) {
		print ("        Test passed\n");
	}
	else {
		print ("        Test FAILED\n");

		foreach my $diagnosticText (@failureDiagnostic) {
			print ("            " . $diagnosticText . "\n");
		}
	}

	Test_StopAll ();

	print ("\n");
	return ($Result);
}


return 1;
