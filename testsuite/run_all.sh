#!/bin/sh
for i in test_*.pl ; do
	echo '-' $i ;
	./$i --verbose;
done
