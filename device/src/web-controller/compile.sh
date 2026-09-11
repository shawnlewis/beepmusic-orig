#!/bin/sh

COFFEE_FLAGS="-c -m -o gen"

if [ $1 ]; then
    COFFEE_FLAGS="$COFFEE_FLAGS -w"
fi

coffee $COFFEE_FLAGS network.coffee main.coffee
