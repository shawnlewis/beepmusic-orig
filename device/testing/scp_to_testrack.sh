#!/bin/sh

cd ../openwrt

TESTDEVS="beep-00dc97 beep-00dc9a beep-00dcc4 beep-00dcdf beep-00dd24 beep-00dd4b beep-00dd5a beep-00ddd8"

for dev in $TESTDEVS; do
    scp $1 $dev.local:$2 &
done
wait
