#!/bin/sh
#
# Converts an input mp3 to all sample rates supported by lame, with mono
# and stereo variants of each. Truncates each output to the first 30 seconds.
#
# Need these packages
# sudo apt-get install lame sox libsox-fmt-mp3
#
# Ex: cd ../testdata ; ../scripts/create_mp3_variants.sh mckee

RATES="8000 11025 12000 16000 22050 24000 32000 44100 48000"

IN=$1

for rate in $RATES ; do
    sox $IN.mp3 /tmp/_.wav rate $rate trim 0 30 ; lame /tmp/_.wav $IN-$rate.mp3
    sox $IN.mp3 /tmp/_.wav rate $rate trim 0 30 ; lame -m m /tmp/_.wav $IN-$rate-mono.mp3
done

rm -f /tmp/_.wav
