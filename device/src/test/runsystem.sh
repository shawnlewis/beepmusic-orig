#!/bin/bash

echo "### Stopping all devices ###"
boom allstop
sleep 1
echo "### Starting logstash (10s delay) ###"
./logmon &
sleep 10
echo "### Starting systemtest.py ###"
python systemtest.py -v $@
