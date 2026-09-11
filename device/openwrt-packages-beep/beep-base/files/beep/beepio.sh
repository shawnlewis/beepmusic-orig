#!/bin/sh

set -e   # exit on first error

. /beep/beep_functions.sh

trap kill_children SIGINT SIGTERM

cd /beep/platform

. /beep/env.sh

/beep/beepmanagerd_stop.sh

lua lua/beepio.lua --uciconfig=/etc/config &
wait
