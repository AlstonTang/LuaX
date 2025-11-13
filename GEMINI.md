# Project Overview

This project is a Lua to C++ translator, named "LuaX". It takes Lua source files as input, translates them into C++ code, and then compiles the C++ code into an executable. The project includes a runtime library in C++ to support Lua's dynamic features, such as dynamic objects and metatables.

## Building and Running

The project can be built and run for a specific Lua file using the `scripts/luax.lua` script:

```bash
lua5.4 scripts/luax.lua <path_to_lua_file> <path_to_output_executable>
```

This script performs the following steps:
1.  Analyzes the provided Lua file for dependencies.
2.  Translates the main Lua file and all its dependent Lua files into C++ files located in the `build/` directory.
3.  Compiles the generated C++ code along with the runtime library (`lib/lua_object.cpp` and others) into an executable.

## Development Conventions

The primary entry point for translating individual Lua files is `scripts/luax.lua`. The translator itself is composed of modules like `src/translator.lua` (for AST generation) and `src/cpp_translator.lua` (for C++ code generation). The C++ runtime library in `lib/` and `include/` provides the necessary support for Lua features in the compiled C++ code.

---

# Agent Progress Summary

## Implemented Libraries and Global Functions/Constants

The following Lua standard libraries and global functions/constants have been successfully implemented and integrated into the LuaX translator:

*   **Libraries:**
    *   `math`
    *   `string` (missing `dump`, `format`, `pack`, `packsize`, `unpack`)
    *   `table` (missing `pack`)
    *   `os`
    *   `io` (missing `popen`, `file:lines()`, `file:setvbuf()`)
*   **Global Functions/Constants:**
    *   `_VERSION`
    *   `print`
    *   `rawequal` (implicitly handled by `lua_equals`)
    *   `rawget`
    *   `rawset` (implicitly handled by direct table assignment)
    *   `setmetatable`
    *   `tonumber`
    *   `tostring`
    *   `type`
    *   `error` (throws `std::runtime_error`)
    *   `pcall` (fully implemented and correctly catches exceptions)
    *   `getmetatable`
    *   `assert`
    *   `collectgarbage`
    *   `select`
    *   `warn`
    *   `xpcall`


## Immediate Next Steps

1.  **Complete Core Lua Libraries:** Continue porting the remaining core Lua libraries from `vars.md`, including:
    *   `coroutine`
    *   `debug`
    *   `utf8`
    *   `package`
2.  **Implement Remaining Global Functions:** Implement the rest of the global functions and constants from `vars.md`, such as:
    *   `dofile`
    *   `load`
    *   `loadfile`
    *   `next`
    *   `require`
3.  **Verify Native Lua Table Indexing:** Implement proper Lua table indexing (e.g., `my_table[i]`) directly in the translator to remove the current reliance on `rawget` in Lua code for array-like access.
4.  **Comprehensive Testing:** Ensure all implemented features are thoroughly tested with a robust set of unit and integration tests.