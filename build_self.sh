#!/bin/bash
set -e

echo "--- Step 1: Compile LuaX using standard Lua ---"
# We use the existing src/luax.lua (running on lua5.4) to compile src/luax.lua itself into a C++ binary.
# Output binary will be build/luax
lua5.4 src/luax.lua src/luax.lua build/luax

echo "--- Step 2: Verify the compiled LuaX binary ---"
if [ -f build/luax ]; then
    echo "LuaX binary created successfully."
else
    echo "Failed to create LuaX binary."
    exit 1
fi

echo "--- Step 3: Use the compiled LuaX to compile an example ---"
# Now use the *compiled* luax to compile tests/main.lua
./build/luax tests/main.lua build/main_self_compiled

echo "--- Step 4: Run the example compiled by the self-compiled LuaX ---"
./build/main_self_compiled

echo "--- Self-Compilation Test Passed! ---"
