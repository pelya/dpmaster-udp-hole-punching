#!/usr/bin/perl -w

use strict;
use testlib;


Master_SetProperty ("cmdlineoptions", "-n 0");
Master_SetProperty ("exitvalue", 1);

Test_Run ("Maximum number of servers set to zero on the command line");
