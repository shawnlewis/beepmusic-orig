#!/bin/bash

if [ ! -d optmnt ]; then
    mkdir optmnt
fi

sudo mount -o loop,offset=1048576 disk.img optmnt

if [ ! -d optmnt/packages ] ; then
    sudo mkdir optmnt/packages
fi

sudo cp ../../openwrt/bin/malta/packages/* optmnt/packages

sudo umount optmnt

