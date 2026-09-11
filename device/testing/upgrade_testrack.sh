#!/bin/sh

cd ../openwrt

TESTDEVS="beep-00dc97 beep-00dc9a beep-00dcc4 beep-00dcdf beep-00dd24 beep-00dd4b beep-00dd5a beep-00ddd8"

for dev in $TESTDEVS; do
    ssh $dev.local "/etc/init.d/beepmanager stop" &
done
wait


for dev in $TESTDEVS; do
    scp bin/ar71xx/openwrt-ar71xx-generic-carambola2-squashfs-sysupgrade.bin root@$dev.local:/tmp &
done
wait

for dev in $TESTDEVS; do
    ssh -n -f $dev.local "/bin/sh -c 'nohup sysupgrade -v /tmp/openwrt-ar71xx-generic-carambola2-squashfs-sysupgrade.bin > /dev/null 2>&1 &'" &
done
wait
