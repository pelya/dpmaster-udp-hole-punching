#!/usr/bin/perl -w

use strict;
use testlib;


Master_SetProperty ("cmdlineoptions", "-v -n 4");

Test_Run ("No parameter associated with the verbose command line option");
