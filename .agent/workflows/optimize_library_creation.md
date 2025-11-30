---
description: How to optimize Lua library creation functions for faster startup
---

To optimize the startup time of LuaX, library creation functions (e.g., `create_math_library`) should be refactored to use direct map initialization for the `properties` member of the `LuaObject`. This avoids the overhead of repeated `set()` calls, which involve string construction, map lookups, and virtual function calls.

## Steps

1.  **Identify the Library Creation Function**: Locate the function responsible for creating the library (e.g., `create_math_library` in `lib/math.cpp`).

2.  **Replace `set()` Calls with Initializer List**:
    Instead of:
    ```cpp
    auto lib = std::make_shared<LuaObject>();
    lib->set("func1", std::make_shared<LuaFunctionWrapper>(func1_impl));
    lib->set("func2", std::make_shared<LuaFunctionWrapper>(func2_impl));
    return lib;
    ```
    Use:
    ```cpp
    auto lib = std::make_shared<LuaObject>();
    lib->properties = {
        {"func1", std::make_shared<LuaFunctionWrapper>(func1_impl)},
        {"func2", std::make_shared<LuaFunctionWrapper>(func2_impl)}
    };
    return lib;
    ```

3.  **Handle Constants**:
    Constants can also be included in the initializer list:
    ```cpp
    lib->properties = {
        {"pi", 3.14159},
        {"huge", std::numeric_limits<double>::infinity()}
    };
    ```

4.  **Handle Complex Initialization**:
    If some properties require complex setup (e.g., creating a metatable first), do that *before* the initializer list if possible, or use `set()` for those specific items *after* the list initialization (though mixing is less efficient than doing it all in one go if possible).
    
    For example, if `file_metatable` is needed for `io` library:
    ```cpp
    file_metatable = std::make_shared<LuaObject>();
    file_metatable->properties = { ... }; // Initialize metatable first
    
    auto io_lib = std::make_shared<LuaObject>();
    io_lib->properties = {
        {"open", ...},
        // ...
    };
    ```

5.  **Inline Helper Functions**:
    Mark frequently used helper functions (like `get_number`, `get_string`) as `inline` to reduce function call overhead. If they are used across multiple translation units, move their definition to the header file (`include/lua_object.hpp` or similar) to allow the compiler to inline them everywhere.

6.  **Verify Compilation**:
    Run `./build_self.sh` to ensure the changes compile and the self-hosted LuaX works correctly.
