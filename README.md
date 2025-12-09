# LuaX: A Lua to C++ Transpiler

LuaX is a robust transpiler that converts Lua 5.4 source code into C++17, allowing you to compile Lua scripts into standalone native executables. It bridges the gap between Lua's dynamic flexibility and C++'s performance and portability.

## Features

*   **Lua 5.4 Support**: Supports a wide range of Lua 5.4 syntax and semantics.
*   **Standard Library**: Includes implementations for most Lua standard libraries:
	*   `math`: Full support (trigonometry, random, etc.).
	*   `string`: Pattern matching, formatting, and manipulation.
	*   `table`: Sorting, packing/unpacking, and manipulation.
	*   `io`: File I/O, `popen`, `tmpfile`, `lines`, and more.
	*   `os`: System interaction, date/time, and execution.
	*   `utf8`: UTF-8 string support.
	*   `coroutine`: **Thread-based implementation** supporting parallelism (using `await` and `create_parallel`)
	*   `package`: Basic module loading support.
*   **C++ Integration**: Generates readable C++ code that uses a custom runtime library (`LuaValue`, `LuaObject`) to emulate Lua's dynamic typing.
*   **Standalone Executables**: Compiles your Lua scripts directly into native binaries.

## Prerequisites

*   **Lua 5.4**: Required to run the transpiler script (`src/luax.lua`).
*   **C++ Compiler**: A C++17 compliant compiler (e.g., `clang++`, `g++`).
*   **Make**: For the build process.

## Quick Start

### Building and Running a Lua Script

To transpile and compile a Lua script (e.g., `tests/main.lua`):

```bash
lua5.4 src/luax.lua -k tests/main.lua build/my_program build
```

This command will:
1.  Transpile `tests/main.lua` (and its dependencies) to C++ in the `build/` directory.
2.  Compile the generated C++ code and the LuaX runtime library.
3.  Produce an executable named `build/my_program`.
4.  Build within the build directory 
*   Note that the `-k` flag means we preserve the transpiled code. In the future, we may make the compiler emit clean, thread-safe, memory-safe C++. You can substitute `-k` with `--keep`, or you can omit it entirely if you do not want to see the transpiled code.

Then, simply run the executable:

```bash
./build/my_program
```

### Examples

Check out the `tests/` directory for sample scripts:
*   `tests/main.lua`: General feature test.
*   `tests/test_io_missing.lua`: Tests for IO functions (`popen`, `tmpfile`, etc.).
*   `tests/test_coroutines.lua`: Tests for coroutine functionality.

## Architecture

LuaX works by traversing the Lua AST (Abstract Syntax Tree) and generating equivalent C++ code.
*   **`src/luax.lua`**: The main entry point and build orchestrator.
*   **`src/translator.lua`**: Parses Lua code into an AST.
*   **`src/cpp_translator.lua`**: Converts the AST into C++ code.
*   **`lib/`**: Contains the C++ runtime library (`LuaValue`, `LuaObject`, `LuaCoroutine`, etc.) and standard library implementations (`math.cpp`, `io.cpp`, etc.).

## Limitations

*   **`debug` Library**: Not implemented.
*   **Dynamic Loading**: `load`, `loadfile`, and `dofile` are not supported because the C++ code is compiled ahead-of-time. Use `require` for static dependencies.
*   **Garbage Collection**: The runtime uses C++ smart pointers (`std::shared_ptr`) for memory management, which differs from Lua's garbage collector (e.g., reference counting vs. mark-and-sweep). Cycle detection is not currently implemented.
*   **Speed**: Currently not very fast (yet). The intent is to ensure feature-compatability before we go in and actually make it faster.

## License

[Apache 2.0](LICENSE)
