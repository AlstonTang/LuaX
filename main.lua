local translator = require("translator")
local cpp_translator = require("cpp_translator")

-- More complex Lua code example to test translation
local code = [[
local myTable = {10, "hello", key = 100, anotherKey = "world"}
local result = myLib.calculate(myTable.key, "test")
local message = "Processing complete: " .. result .. ".\n"
print(message)

x = (10 + 20) * 30
local y = x / 2
]]

print("--- Original Lua Code ---")
print(code)

-- Parse the Lua code into an AST
local root_node = translator.translate(code)

-- Translate the AST to C++ code
local cpp_code = cpp_translator.translate(root_node)

print("\n--- Generated C++ Code ---")
print(cpp_code)

-- Optional: Save the generated C++ code to a file
-- local file = io.open("output.cpp", "w")
-- file:write(cpp_code)
-- file:close()
-- print("\nC++ code saved to output.cpp")
