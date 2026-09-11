if [ $# -ne 1 ]; then
    echo "Usage: print_thread_info.sh <process name>"
    exit 1
fi

PROCESS_NAME=$1

TIDS=`ps -eLf | grep $PROCESS_NAME | awk '{print $4}'`

for tid in $TIDS; do
    echo $tid
    sudo gdb --pid $tid -batch -ex "bt" 2>&1 | grep "0x.* in "
    echo
done
