#!/usr/bin/perl -w

use strict;
use testlib;


my $cmdlineoptions = Master_GetProperty ("cmdlineoptions");
Master_SetProperty ("cmdlineoptions", $cmdlineoptions . " -v");

Test_Run ("No parameter associated with the verbose command line option");
