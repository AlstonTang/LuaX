#!/bin/bash
set -e

# We use the existing src/luax.lua (running on lua5.4) to compile src/luax.lua itself into a C++ binary.
# Output binary will be build/luax
lua5.4 src/luax.lua src/luax.lua build/luax

if [ -f build/luax ]; then
    echo "LuaX binary created successfully."
else
    echo "Failed to create LuaX binary."
    exit 1
fi