#!/bin/bash
# LuaX Development Setup Script
# This script builds the runtime library and configures C++ tooling.
#
# For end-users: This is optional - the transpiler (luax.lua) works without it.
# For developers: Run this once to enable IDE features (clangd, IntelliSense).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

echo "============================================"
echo "LuaX Development Setup"
echo "============================================"
echo ""

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo "CMake not found. Installing..."
    if command -v apt-get &> /dev/null; then
        sudo apt-get update && sudo apt-get install -y cmake
    elif command -v brew &> /dev/null; then
        brew install cmake
    else
        echo "ERROR: Please install CMake manually."
        exit 1
    fi
fi

# Create build directory
mkdir -p "${BUILD_DIR}"

# Configure with CMake (generates compile_commands.json)
echo "[1/3] Configuring CMake..."
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Build the runtime library
echo ""
echo "[2/3] Building runtime library..."
cmake --build "${BUILD_DIR}" -j

# Also build the precompiled object files for the Makefile-based transpiler
echo ""
echo "[3/3] Building object files for transpiler..."
cd "${SCRIPT_DIR}/lib"
for cpp_file in *.cpp; do
    obj_file="${cpp_file%.cpp}.o"
    if [ ! -f "$obj_file" ] || [ "$cpp_file" -nt "$obj_file" ]; then
        echo "  Compiling ${cpp_file}..."
        g++ -std=c++17 -I"${SCRIPT_DIR}/include" -O2 -c "$cpp_file" -o "$obj_file"
    fi
done
cd "${SCRIPT_DIR}"

echo ""
echo "============================================"
echo "Setup Complete!"
echo "============================================"
echo ""
echo "What was set up:"
echo "  ✓ compile_commands.json for C++ tooling (clangd/IntelliSense)"
echo "  ✓ libluax_runtime.a in build/"
echo "  ✓ Precompiled .o files in lib/"
echo ""
echo "You can now:"
echo "  - Open VSCode/your IDE for full C++ symbol resolution"
echo "  - Run 'bash build_self.sh' to self-compile LuaX"
echo "  - Run 'lua5.4 src/luax.lua <input.lua> <output> <build_dir>' to transpile"
echo ""
