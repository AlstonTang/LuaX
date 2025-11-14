# Project Overview

This project is a Lua to C++ translator, named "LuaX". It takes Lua source files as input, translates them into C++ code, and then compiles the C++ code into an executable. The project includes a runtime library in C++ to support Lua's dynamic features, such as dynamic objects and metatables.

## Building and Running

The project can be built and run for a specific Lua file using the `scripts/luax.lua` script:

```bash
lua5.4 src/luax.lua <path_to_lua_file> <path_to_output_executable>
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
    *   `string` (missing `dump`, `format`, `pack`, `packsize`, `unpack`, but regex functions are now correctly handled)
    *   `table`
    *   `os`
    *   `io` (missing `popen`, `file:lines`, `file:setvbuf`)
    *   `package`
    *   `utf8`
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

## Unimplemted Libraries
*   `debug` (This makes little sense given the context of code being compiled)
*   `coroutine` (Not implemented yet. Quite complicated currently.)
*   Any variable pertaining with dynamic lua execution (for now).

## Resolved Issues

All issues previously listed in `ISSUES.md` have been addressed:
*   Compilation errors related to `std::regex` and `string.find`/`gsub` in `examples/main.lua` are fixed.
*   Compilation errors related to return type mismatch in `examples/other_module.lua` are fixed.
*   Hexadecimal literals in `examples/test_utf8.lua` are now correctly parsed.

## Long-Term Goals

1.  **Comprehensive Testing:** Ensure all implemented features are thoroughly tested with a robust set of unit and integration tests.
    *   This includes be able to use `scripts/luax.lua` to compile itself.
2.  **Type Inference And Optimization:** Implement type inference so that certain types can be natively inferred to reduce usage of variants and to increase performance.
3. **Better error information:** Ensure that any errors found within the luax transpiler can be traced more robustly to either the transpiler itself or the code that it is transpiling.
