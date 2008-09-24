#!/usr/bin/perl -w

use strict;
use testlib;


Master_SetProperty ("cmdlineoptions", "");

my $serverRef = Server_New ();
Server_SetProperty ($serverRef, "cannotBeRegistered", 1);

my $clientRef = Client_New ();

Test_Run ("No servers allowed on loopback interfaces");
