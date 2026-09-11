#!/bin/sh

echo "Starting wifi scan test at `date`"
count=0
while [ 1 ]
do
    iw dev wlan0 scan > /dev/null
    echo "Wifi scan $count complete at `date`"
    count=$((count+1))
done
