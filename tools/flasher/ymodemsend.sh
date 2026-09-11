#!/bin/sh

DEV=$1

stty -F $DEV 115200
sz --ymodem $2 > $DEV < $DEV

exit $?
