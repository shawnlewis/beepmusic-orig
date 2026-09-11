#!/bin/sh

while [ 1 ]; do
    nosetests test_devs.py 2>&1 | tee --append ~/testlog.txt
done
