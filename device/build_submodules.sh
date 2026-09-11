#!/bin/bash -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $SCRIPT_DIR

ASAN=no
if [ $1 ]; then
    ASAN=yes
    echo "Enabling libasan"
fi

# libubox
pushd libubox
rm -f CMakeCache.txt
cmake -DASAN=$1 .
sudo make install
popd

# ubus
pushd ubus
rm -f CMakeCache.txt
cmake -DASAN=$1 .
sudo make install
popd

# uci
pushd uci
rm -f CMakeCache.txt
cmake -DASAN=$1 .
sudo make install
popd

# uhttpd2
pushd uhttpd2
rm -f CMakeCache.txt
cmake -DASAN=$1 -DTLS_SUPPORT=off .
sudo make install
popd
