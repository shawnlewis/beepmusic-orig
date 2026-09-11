#!/bin/bash

echo "--- Initialize boom test devices ---"
if [ $# -ne 1 ]
then
    echo "Usage: init.sh <prefix>"
    exit -1
fi

sed -i "s%\/path\/to\/beep%`readlink -f ../../../`%g" logmon.conf
find . -name "beep" -print | xargs sed -i "s/option device_id '[a-zA-Z_]*_\([A-Z]\)'/option device_id '$1_\1'/g"

find . -name "beep" -print | xargs git update-index --assume-unchanged
