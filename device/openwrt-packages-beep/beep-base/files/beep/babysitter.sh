#!/bin/sh

# don't use, causes a failure when our subprocess dies even though we run it
# with & (bg)
#set -e     # exit on first error

. /beep/beep_functions.sh

trap kill_children SIGINT SIGTERM

CMDPATH=$(basename "$1")
if [ "$CMDPATH" == "lua" ]; then
    CMDPATH="$2"
fi

HOSTNAME=`uci get system.@system[0].hostname`

TAG="$CMDPATH"

while [ 1 ]; do
    # SIGINT won't interrupt children that are running the foreground, so we
    # background it and pass SIGINT on to children via trap.
    $@ 2>&1 | logger -s -t "$TAG" &

    # gets the pid of the process group owner, which is the actual command
    # rather than the logger process which is what $! would return
    pids="$(jobs -p)"

    # not sure why we can't just pipe to head in the above command, but $pid
    # ends up empty if we do.
    pid=$(echo "$pids" | head -n1)

    echo "babysitter: started $CMDPATH. PID $pid"

    # can't use wait, because that waits for all children including the logger
    # process that we've piped to.
    # 'wait $pid' doesn't seem to work. It returns immediately. So we construct
    # our own version of wait by polling.
    while [ 1 ]; do
        ps | grep $pid | grep -v grep 2>&1 > /dev/null
        if [ $? -ne 0 ]; then
            break
        #else
        #    echo "PID still alive $pid"
        fi
        sleep 1
    done

    echo "babysitter: $CMDPATH complete."
    sleep 1
done
