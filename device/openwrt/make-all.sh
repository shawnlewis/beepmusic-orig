#!/bin/sh
ionice -c 3 nice -n 20 make -j 9 $@
