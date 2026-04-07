---
name: updating_luax
description: Helps describe best practices for updating LuaX.
---

# Updating LuaX
The following document helps support your development experience and helps to reduce common pain points.

## Directories
LuaX is roughly structured as follows:
- src (Transpiler files)
- lib (LuaX Standard Library Implementation)
- include (LuaX Standard Library Headers)
- tests (Test files)

Keep in mind it may be useful every now and then to check the file structure to get a more concrete picture.

## Planning
Prior to implementation of what the user requests, you should analyze the file structure, contents (as needed), and try and reason through what's going on. You may already be doing this, but I would like to emphasize that it is especially important to have a well-thoughout plan for such a complex codebase like LuaX.

## Editing
When editing LuaX, there is usually three main categories:
1. LuaX terminal script
2. The transpiler itself
3. The LuaX Standard Library

Usually, depending on task, you'll find yourself doing one of these three, but more often than not you may need to performtasks outside of the primary category. This is normal and okay.

### LuaX terminal script
This file is located at src/luax.lua. Usually, this script deals with console input and console output.
- There may be some profiling print statements spread across src.

### The transpiler itself
When editing the transpiler, there are a few things to keep in mind:
1. Try not to microoptimize (e.g. aggresive and very long local declarations). This isn't very helpful (although some parts of the codebase may do this, it is not recommended to do so for future work), and causes a lot of bloat.
2. The C++ code generator should respect the interfaces outlined within the lib and include directories.
	- This doesn't mean that your code

### The LuaX Standard Library
When editing the standard library, keep in mind the following:
1. Efficiency. Try to make your code as concise as possible. Although it may not be the most performant, readable code is often easier to maintain and actually can be more performant in the long run
2. LuaX uses the C++20 standard.
3. Methods often have a very similar function structure. This could mean that you could run a find and replace mechanism to help accelerate development instead of manually editing one file at a time (do be mindful of certain edge-cases).

## Testing
Once your changes are made, here is the easiest way to test those changes:
1. Write a test (optional). This depends on task scope, so you may not need to even write a test.
2. Self-transpile the transpiler and compile it. Internally, it uses CMake, but usually you should run clean_compile_self.sh to minimize any potential mishaps in command execution.
3. Using the compiled transpiler, transpile what it is you need to test. Sometimes, it may be a test within the tests directory. Other times, it may be the transpiler itself.
	- When transpiling the transpiler using the natively compiled transpiler, run `./build/luax -t src/luax.lua -b build_native -o build_native/luax`. Note that if explicitly testing single-threaded performance, add the `-s` flag. Otherwise, leave it as is.
4. Fix any errors

You should be testing often (if changes are independent of each other). The only time testing may be done as a unit is if changes depend on each other (e.g. New design iteration means that changes are needed both within the transpiler and the standard library).

### Debugging
Sometimes, it may be very tempting to run a full-on gdb. This can clutter your context. Instead, here's a suggestion on how to debug errors:
1. Determine whether it's a formatting error or a logical error by looking at the most recent changes.
2. If it's a formatting error, the fix should be relatively trivial
3. If it's a logical error, try to analyze what went wrong. Only when it gets very unwieldy should gdb be used. If necessary, rollback some files through git and start over.

Do not, under any circumstances, put debug prints in hot loops. This will severly clutter development and make things super hard to debug.

## Forks in the Road
Sometimes, it may be the case that things are just too large of a scope to complete successfully. In that case, step back, think of a new plan, and propose it like you would with implementation plans.

## Conclusion
This document should serve as a representative guide to updating LuaX. Should you have any further questions, please notify the user. The last thing that is needed is for invalid assumptions to derail the project.