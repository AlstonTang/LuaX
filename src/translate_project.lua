local translator = require("src.translator")
local cpp_translator = require("src.cpp_translator")

local function translate_file(lua_file_path, output_file_name)
    print("DEBUG: Starting translation for file: " .. lua_file_path)
    print("Translating " .. lua_file_path .. "...")
    local file = io.open(lua_file_path, "r")
    if not file then
        error("Could not open " .. lua_file_path)
    end
    local lua_code = file:read("*all")
    file:close()

    local ast = translator.translate(lua_code)
    -- DEBUG: Print AST
    print("--- AST for " .. lua_file_path .. " ---")
    print(require("src.ast_printer").print_ast(ast))
    print("--- End AST for " .. lua_file_path .. " ---")
    local cpp_code = cpp_translator.translate_recursive(ast, output_file_name, false) -- for .cpp
    local hpp_code = cpp_translator.translate_recursive(ast, output_file_name, true) -- for .hpp

    local cpp_output_path = "build/" .. output_file_name .. ".cpp"
    local hpp_output_path = "build/" .. output_file_name .. ".hpp"

    local cpp_file = io.open(cpp_output_path, "w")
    if cpp_file then
        cpp_file:write(cpp_code)
        cpp_file:close()
        print("Generated " .. cpp_output_path)
    else
        error("Could not write to " .. cpp_output_path)
    end

    local hpp_file = io.open(hpp_output_path, "w")
    if hpp_file then
        hpp_file:write(hpp_code)
        hpp_file:close()
        print("Generated " .. hpp_output_path)
    else
        error("Could not write to " .. hpp_output_path)
    end
end

-- Translate other_module.lua
translate_file("src/other_module.lua", "other_module")

-- Translate main.lua
translate_file("src/main.lua", "main")

print("Translation complete.")