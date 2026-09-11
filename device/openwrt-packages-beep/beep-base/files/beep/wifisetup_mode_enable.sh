#!/bin/sh

/etc/init.d/beepmanager stop

# Disable bones because core uploader doesn't work when we're in AP mode!
/etc/init.d/bones stop

/etc/init.d/dnsmasq start
/etc/init.d/uhttpd_setup start
