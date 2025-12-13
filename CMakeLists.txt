# ==============================================================================
# LuaX CMake Configuration
# ==============================================================================
# PURPOSE: This CMakeLists.txt is for DEVELOPING the LuaX runtime library.
# It provides:
#   - compile_commands.json for C++ tooling (clangd, IntelliSense)
#   - libluax_runtime.a static library
#   - Proper IDE integration
#
# NOTE: This is NOT used when transpiling user Lua programs!
# The transpiler (src/luax.lua) dynamically generates a Makefile for each
# project to remain directory-agnostic. See README.md for details.
# ==============================================================================

cmake_minimum_required(VERSION 3.16)
project(LuaX VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for C++ tooling (clangd, IntelliSense, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type defaults to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# ------------------------------------------------------------------------------
# LuaX Runtime Library (static library for linking into generated binaries)
# ------------------------------------------------------------------------------

set(LUAX_LIB_SOURCES
    lib/lua_object.cpp
    lib/coroutine.cpp
    lib/debug.cpp
    lib/init.cpp
    lib/io.cpp
    lib/math.cpp
    lib/os.cpp
    lib/package.cpp
    lib/string.cpp
    lib/table.cpp
    lib/utf8.cpp
        build/luax.cpp
        build/src_tokenizer.cpp
        build/src_node.cpp
        build/src_cpp_translator.cpp
)

set(LUAX_HEADERS
    include/lua_value.hpp
    include/lua_object.hpp
    include/coroutine.hpp
    include/debug.hpp
    include/init.hpp
    include/io.hpp
    include/math.hpp
    include/os.hpp
    include/package.hpp
    include/string.hpp
    include/table.hpp
    include/utf8.hpp
)

add_library(luax_runtime STATIC ${LUAX_LIB_SOURCES})

target_include_directories(luax_runtime 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(luax_runtime PUBLIC cxx_std_17)

# Link pthread for coroutine support
find_package(Threads REQUIRED)
target_link_libraries(luax_runtime PUBLIC Threads::Threads)

# On older systems, we may need stdc++fs for filesystem support
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(luax_runtime PUBLIC stdc++fs)
endif()

# ------------------------------------------------------------------------------
# Platform-specific settings
# ------------------------------------------------------------------------------
if(MSVC)
    target_compile_options(luax_runtime PRIVATE /W4)
else()
    target_compile_options(luax_runtime PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# ------------------------------------------------------------------------------
# Installation (optional, for packaging)
# ------------------------------------------------------------------------------
install(TARGETS luax_runtime
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES ${LUAX_HEADERS} DESTINATION include/luax)

# ------------------------------------------------------------------------------
# Helper function for building transpiled Lua files
# Usage from build directory after running transpiler:
#   cmake -DLUAX_TRANSPILED_SOURCES="main.cpp;module.cpp" -DLUAX_TARGET_NAME="myapp" ..
# ------------------------------------------------------------------------------
if(DEFINED LUAX_TRANSPILED_SOURCES AND DEFINED LUAX_TARGET_NAME)
    add_executable(${LUAX_TARGET_NAME} ${LUAX_TRANSPILED_SOURCES})
    target_link_libraries(${LUAX_TARGET_NAME} PRIVATE luax_runtime)
endif()

# ------------------------------------------------------------------------------
# Print configuration summary
# ------------------------------------------------------------------------------
message(STATUS "")
message(STATUS "LuaX Configuration:")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
