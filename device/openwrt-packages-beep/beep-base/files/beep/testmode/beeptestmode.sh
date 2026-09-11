#!/bin/sh

mode=freqsweep

LED=/sys/class/leds/beep\:red\:setup

echo timer > $LED/trigger

while [ 1 ]; do
    if [ -e /tmp/BUTTON_SINGLE_PRESS ]; then
        rm /tmp/BUTTON_SINGLE_PRESS
        if [ $mode == "freqsweep" ]; then
            mode=volumesweep
        elif [ $mode == "volumesweep" ]; then
            mode=maxvolume
        else
            mode=freqsweep
        fi
    fi

    if [ $mode == "freqsweep" ]; then
        echo "freqsweep"
        echo 500 > $LED/delay_on
        echo 500 > $LED/delay_off
        athplay /beep/testmode/freq-sweep.wav
    elif [ $mode == "volumesweep" ]; then
        echo "volumesweep"
        echo 1500 > $LED/delay_on
        echo 500 > $LED/delay_off
        athplay /beep/testmode/vol-sweep.wav
    elif [ $mode == "maxvolume" ]; then
        echo "maxvolume"
        echo 500 > $LED/delay_on
        echo 50 > $LED/delay_off
        athplay /beep/testmode/1004.wav
    fi
done
