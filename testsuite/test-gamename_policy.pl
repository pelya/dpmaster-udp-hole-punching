#!/usr/bin/perl -w

use strict;
use testlib;


my @games = (
	{
		name => "dpmasterTest1",
		cannotBeRegistered => 0,
	},
	{
		name => "dpmasterTest2",
		cannotBeRegistered => 0,
	},
	{
		name => "dpmasterTest3",
		cannotBeRegistered => 1,
	},
	{
		name => "dpmasterTest4",
		cannotBeRegistered => 1,
	},
);

foreach my $gameRef (@games) {
	my $gameName = $gameRef->{name};

	# Create the server
	my $serverRef = Server_New ();
	Server_SetProperty ($serverRef, "id", $gameName);
	Server_SetProperty ($serverRef, "cannotBeRegistered", $gameRef->{cannotBeRegistered});
	Server_SetGameProperty ($serverRef, "gamename", $gameName);

	# Create the associated client
	my $clientRef = Client_New ();
	Client_SetGameProperty ($clientRef, "gamename", $gameName);
}

my $cmdlineoptions = Master_GetProperty ("cmdlineoptions");


my $newCmdlineoptions = $cmdlineoptions . " --game-policy accept dpmasterTest1 dpmasterTest2 dpmasterTest5 dpmasterTest6";
Master_SetProperty ("cmdlineoptions", $newCmdlineoptions);

Test_Run ("Game policy using \"accept\"");


$newCmdlineoptions = $cmdlineoptions . " --game-policy reject dpmasterTest3 dpmasterTest4 dpmasterTest5 dpmasterTest6";
Master_SetProperty ("cmdlineoptions", $newCmdlineoptions);

Test_Run ("Game policy using \"reject\"");
