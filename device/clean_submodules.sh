#!/bin/bash -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $SCRIPT_DIR

# ubus
pushd ubus
git clean -xdf
popd

# libubox
pushd libubox
git clean -xdf
popd

# uci
pushd uci
git clean -xdf
popd

# uhttpd2
pushd uhttpd2
git clean -xdf
popd
