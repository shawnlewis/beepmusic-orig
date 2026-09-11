#!/bin/sh

#set -e   # exit on first error

. /beep/beep_functions.sh

trap kill_children SIGINT SIGTERM

# make sure there aren't any old locks
rm -f /var/run/opkg.lock

source /beep/env.sh

/beep/beepmanagerd_stop.sh

# Network state might have recently cycled -- make sure mdnsd is still ballin'
/etc/init.d/mdnsd stop
/etc/init.d/mdnsd start

rm -f /dev/i2s
# || /bin/true suppresses the error that occurs if no processes are killed
rmmod ath_i2s || /bin/true
insmod ath_i2s
if [ $? != 0 ] ; then
    echo "ERROR: Could not insert ath_i2s, Rebooting!"
    /sbin/reboot
fi
mknod /dev/i2s c 253 0

cd /beep/platform
lua lua/beepmanager.lua --ubus=/var/run/ubus.sock --uciconfig=/etc/config &
wait
