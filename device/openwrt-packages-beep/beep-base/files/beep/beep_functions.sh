kill_children() {
    JOBS=$(jobs -p)
    echo Signal received killing: $JOBS
    [ -n "$JOBS" ] && kill $JOBS
    exit 0
}

wait_for_connection() {
    while [ 1 ]; do
        ping -c 1 -w 2 google.com
        if [ $? == 0 ]; then
            break
        fi
        sleep 1
    done
}

contains() {
    echo "$1" | grep "$2" > /dev/null
}
