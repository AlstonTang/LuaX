#!/bin/bash
set -e

# Automates the file generation process
lua5.4 src/luax.lua -t src/luax.lua -b build -o build/luax