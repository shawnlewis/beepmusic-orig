#!/bin/sh

killall slowtest
killall athclkchk

/usr/bin/slowtest &
