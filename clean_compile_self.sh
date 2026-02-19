#!/bin/bash
set -e

# Automates the file generation process
[ -d "./build" ] && rm -r ./build
lua5.4 src/luax.lua -t src/luax.lua -b build -o build/luax
cmake -S build -B build
cmake --build build -j