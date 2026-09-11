#!/bin/sh

device=$1
uboot=../../uboot/beep1/uboot_beep_cm2.bin

scp ../../src/out/beeptwo/unlockmtds root@$device:/tmp
scp $uboot root@$device:/tmp

ssh $device "chmod 755 /tmp/unlockmtds"
ssh $device "/tmp/unlockmtds"
ssh $device "mtd erase mtd0"
ssh $device "dd if=/tmp/uboot_beep_cm2.bin of=/dev/mtd0"

