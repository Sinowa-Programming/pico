#!/bin/sh
# Filter out the -pthread flag, then pass everything else to the real compiler
args=$(echo "$@" | sed "s/-pthread//g")
exec arm-none-eabi-gcc $args