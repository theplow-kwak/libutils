#!/bin/bash

PARAM=${1:-"op_copy.cpp"}
fname=$(echo "$PARAM" | cut -d'.' -f1)
ext=$(echo "$PARAM" | cut -s -d'.' -f2)
ext=${ext:-"cpp"}

files=""

if [ "$fname" = "offset2lba" ]; then
    files="offset2lba_linux.cpp"
fi

outdir="build"
mkdir -p "$outdir"

echo g++ -std=c++2a -Wall -g -O0 -static -o "$outdir/$fname" "$fname.$ext" $files
g++ -std=c++2a -Wall -g -O0 -static -o "$outdir/$fname" "$fname.$ext" $files