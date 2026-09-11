#!/bin/sh

echo
echo
echo "****************************************"
echo "After this starts: telnet localhost 4444"
echo "****************************************"
echo
echo

sudo openocd -f 4232.cfg -f board.cfg -f ar9331.cfg -f scripts.cfg
