#!/bin/sh
for i in test-*.pl ; do
	echo '-' $i ;
	./$i --verbose;
done
