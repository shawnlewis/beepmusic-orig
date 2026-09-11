#!/bin/sh

echo "Starting net up/down test at `date`"
count=0
while [ 1 ]
do
    /etc/init.d/network stop
    sleep 10
    /etc/init.d/network start
    sleep 30
    echo "Net up/down $count complete at `date`"
    count=$((count+1))
done
