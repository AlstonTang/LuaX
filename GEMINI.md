# Project Overview

This project is a Lua to C++ translator, named "LuaX". It takes Lua source files as input, translates them into C++ code, and then compiles the C++ code into an executable. The project includes a runtime library in C++ to support Lua's dynamic features, such as dynamic objects and metatables.

## Building and Running

The project can be built and run using the `autotest.bash` script:

```bash
./autotest.bash
```

This script performs the following steps:
1.  Translates the Lua files in the `examples/` directory into C++ files located in the `build/` directory.
2.  Compiles the generated C++ code along with the runtime library (`lib/lua_object.cpp`) into an executable named `luax_app` in the `build/` directory.
3.  Runs the `luax_app` executable.

## Development Conventions

The core of the project is the translator located in `src/translate_project.lua`. The translator first builds an Abstract Syntax Tree (AST) from the Lua code and then generates the corresponding C++ code. The C++ runtime library in `lib/` and `include/` provides the necessary support for Lua features in the compiled C++ code.

---

# Agent Progress Summary

## Implemented Libraries and Global Functions/Constants

The following Lua standard libraries and global functions/constants have been successfully implemented and integrated into the LuaX translator:

*   **Libraries:**
    *   `math` (partial, `math_tointeger` fixed)
    *   `string` (`len`, `reverse`, `match`, `find`, `gsub`)
    *   `table` (`insert`, `remove`)
    *   `os` (`clock`, `time`)
    *   `io` (`write`, `read`)
    *   `package` (`path`, `cpath`)
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
    *   `pcall` (partially implemented, intended to catch exceptions)

## Current Issues

*   **`pcall` Exception Handling:** The `pcall` function is currently not correctly catching exceptions thrown by the `error` function. Despite `error` successfully throwing `std::runtime_error` and `pcall` having a `try-catch(...)` block, the exception does not seem to propagate correctly, leading to program termination rather than graceful handling by `pcall`.

## Immediate Next Steps

1.  **Debug `pcall` and `error` Interaction:** Investigate why exceptions thrown by `error` are not being caught by `pcall`. This may involve:
    *   Further simplifying the `pcall` implementation to isolate the issue.
    *   Examining the `LuaFunctionWrapper` and `std::function` behavior regarding exception propagation.
    *   Potentially adding more explicit exception handling or re-throwing mechanisms within the `LuaFunctionWrapper` if necessary.

## Long-Term Objectives

1.  **Complete Core Lua Libraries:** Continue porting the remaining core Lua libraries from `vars.md`, including:
    *   `coroutine`
    *   `debug`
    *   `utf8`
2.  **Implement Remaining Global Functions:** Implement the rest of the global functions and constants from `vars.md`, such as:
    *   `_G`
    *   `assert`
    *   `collectgarbage`
    *   `dofile`
    *   `ipairs`
    *   `load`
    *   `loadfile`
    *   `next`
    *   `pairs`
    *   `require`
    *   `select`
    *   `warn`
    *   `xpcall`
3.  **Native Lua Table Indexing:** Implement proper Lua table indexing (e.g., `my_table[i]`) directly in the translator to remove the current reliance on `rawget` in Lua code for array-like access.
4.  **Comprehensive Testing:** Ensure all implemented features are thoroughly tested with a robust set of unit and integration tests.