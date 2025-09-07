#!/bin/bash

PARAM=${1:-"op_copy.cpp"}
fname=$(echo "$PARAM" | cut -d'.' -f1)
ext=$(echo "$PARAM" | cut -s -d'.' -f2)
ext=${ext:-"cpp"}

if [ "$fname" = "offset2lba" ]; then
    g++ -std=c++2a -Wall -g -O0 -static -o offset2lba offset2lba.cpp offset2lba_linux.cpp
    exit $?
fi
g++ -std=c++2a -Wall -g -O0 -static -o $fname $fname.$ext
