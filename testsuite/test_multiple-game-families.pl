#!/usr/bin/perl -w

use strict;
use testlib;


# DarkPlaces
my $dpServerRef = Server_New (GAME_FAMILY_DARKPLACES);
Server_SetProperty ($dpServerRef, "id", "DPServer");

my $dpClientRef = Client_New (GAME_FAMILY_DARKPLACES);

# Quake 3 Arena
my $q3ServerRef = Server_New (GAME_FAMILY_QUAKE3ARENA);
Server_SetProperty ($q3ServerRef, "id", "Q3Server");

my $q3ClientRef = Client_New (GAME_FAMILY_QUAKE3ARENA);

Test_Run ("Servers running games from different game families");
