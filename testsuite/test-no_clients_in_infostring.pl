#!/usr/bin/perl -w

use strict;
use testlib;


my $serverRef = Server_New ();
Server_SetGameProperty ($serverRef, "clients", undef);
Server_SetProperty ($serverRef, "cannotBeRegistered", 1);
my $clientRef = Client_New ();
Test_Run ("No \"clients\" key in the server infostring");
