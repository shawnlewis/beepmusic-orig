#!/bin/bash

FILES=`find . | grep "\.\(cc\|c\|h\|lua\|js\|py\|coffee\)$" \
    | grep -v "/build" \
    | grep -v libspotify \
    | grep -v mongoose \
    | grep -v libds \
    | grep -v external \
    | grep -v dev_release \
    | grep -v "./out" \
    | grep -v Pandora.js \
    | grep -v libpgm \
    | grep -v "iomcu/iar" \
    | grep -v "iomcu/lib" \
    | grep -v "web-controller.*js"`

wc -l $FILES
