# GEMINI.md

## Project Overview

This project appears to be a **Lua parser and Abstract Syntax Tree (AST) generator**. The core components are `translator.lua` and `main.lua`.

*   `translator.lua`: Contains the implementation for a `Node` class (representing AST nodes) and a `Parser` class. The `Parser` is responsible for tokenizing Lua code and building an AST. It supports basic arithmetic expressions, assignments, local variable declarations, table constructors, function calls, and member access.
*   `main.lua`: This script serves as a test or example of how to use the `translator.lua` module. It defines sample Lua code, translates it into an AST using the `translator`, and then iterates through the AST to print its structure.

**Technologies:** Lua

**Architecture:** The architecture follows a typical compiler frontend pattern: Lexer (Tokenizer) -> Parser -> AST.

## Building and Running

*   **Running the translator/parser:** The `main.lua` script demonstrates how to use the translator. To run it, use the command: `lua main.lua`.
*   **Testing:** The `main.lua` script itself acts as a test case by parsing a sample code string and printing the resulting AST. More comprehensive testing would involve adding more complex Lua code snippets to `main.lua` or creating separate test files.

## Development Conventions

*   **Modularity:** The code is reasonably modular, with distinct `Node` and `Parser` classes.
*   **AST Representation:** AST nodes have `type`, `value`, `identifier`, `parent`, and `ordered_children` properties, which is a standard way to represent ASTs.
*   **Error Handling:** Basic error handling is present (e.g., for unclosed strings, expected tokens), but could be more robust.
*   **Testing:** The current testing approach relies on `main.lua` to execute sample code and print the AST. A more formal testing framework (like Busted for Lua) would be beneficial for thorough testing and edge-case handling.
