#!/bin/sh

echo $0 $* 
echo Environment:
env

echo "sleeping for $1"

sleep $1

echo "stopping child"

exit 5
