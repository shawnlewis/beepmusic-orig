#!/bin/sh

# args: %t %p %s %h %e
# args need to match with core_pattern and bcore.
cat | lzop | /beep/bcore sendcore $1 $2 $3 $4 $5
