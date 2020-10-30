#!/bin/sh

if grep sse /proc/cpuinfo > /dev/null ; then 
	./connie_sse $*
else 
	./connie_i386 $*
fi
