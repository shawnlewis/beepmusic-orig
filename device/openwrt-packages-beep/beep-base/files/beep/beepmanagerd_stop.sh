#!/bin/sh
#
# Kills beepmanager and other beep streaming proccesses, with fire.

# Kill existing beepmanagers.
beepmanager_pids=`pgrep -f "lua/beepmanager.lua"`
if [ -n "$beepmanager_pids" ]; then
    echo "Found running beepmanager.lua processes, killing them: $beepmanager_pids"
    for pid in $beepmanager_pids; do
        group_pid=`ps -o pid,pgid | grep $pid | awk '{print $2}'`
        if [ -n "$group_pid" ]; then
            echo "Killing group ($group_pid) for pid: $pid"
            # only do this after checking $group_pid is defined, 'kill -15 -'
            # causes busybox to segfault!
            kill -15 -$group_pid
            sleep 2
            all_proc_pids=`ps -o pid,pgid | grep $group_pid | awk '{print $1}'`
            if [ -n "$all_proc_pids" ]; then
                echo "Found other group pids still alive, killing: $all_proc_pids"
                kill -9 $all_proc_pids
            fi
        fi
    done
    sleep 1
fi

# Kill other rogue processes, depend on the fact that only beep playback processes
# incluce /var/run/ubus.sock in their command lines -- except wifisetup
other_pids=`pgrep -l -f "/var/run/ubus.sock" | grep -v wifisetup | awk '{print $1}'`
if [ -n "$other_pids" ]; then
    echo "Found pids still alive (still!), murdering them: $other_pids"
    kill -15 $other_pids
    sleep 2
    kill -9 $other_pids
    sleep 1
fi
