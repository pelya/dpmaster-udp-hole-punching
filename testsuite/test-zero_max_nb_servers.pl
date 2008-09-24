#!/usr/bin/perl -w

use strict;
use testlib;


my $cmdlineoptions = Master_GetProperty ("cmdlineoptions");
Master_SetProperty ("cmdlineoptions", $cmdlineoptions . " -n 0");

Master_SetProperty ("exitvalue", 1);

Test_Run ("Maximum number of servers set to zero on the command line");
