#!/bin/bash

PARAM=${1:-"op_copy.cpp"}
fname=$(echo "$PARAM" | cut -d'.' -f1)
ext=$(echo "$PARAM" | cut -s -d'.' -f2)
ext=${ext:-"cpp"}

g++ -std=c++2a -Wall -g -O0 -static -o $fname $fname.$ext
