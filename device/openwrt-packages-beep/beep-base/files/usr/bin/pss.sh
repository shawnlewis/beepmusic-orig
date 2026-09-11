for pid in `ls /proc | grep "[0-9]"`; do
    cat /proc/$pid/smaps | grep Pss | awk '{ sum+=$2 } END {print sum}' | tr -d "\n"
    echo -n " "
    cat /proc/$pid/comm | tr -d "\n"
    echo -n " "
    cat /proc/$pid/cmdline
    echo
done 2>/dev/null
