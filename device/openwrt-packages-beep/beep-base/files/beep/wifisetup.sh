#!/bin/sh

set -e   # exit on first error

cd /beep/platform

LD_LIBRARY_PATH=.
export LD_LIBRARY_PATH

./wifisetup --ubus=/var/run/ubus.sock --uciconfig=/etc/config &
