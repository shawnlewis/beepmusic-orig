#!/bin/bash

boom allstop
sleep 0.5
python test.py -v $@
