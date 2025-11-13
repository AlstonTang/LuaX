local cpp_translator = require("src.cpp_translator")

local translator_chunk, err = loadfile("src/translator.lua")
if not translator_chunk then
    error("Error loading src/translator.lua: " .. err)
end
local translator = dofile("src/translator.lua")

local function translate_file(lua_file_path, output_file_name)
    print("Translating " .. lua_file_path .. "...")
    local file = io.open(lua_file_path, "r")
    if not file then
        error("Could not open " .. lua_file_path)
    end
    local lua_code = file:read("*all")
    file:close()

    local ast = translator.translate(lua_code)
    local cpp_code
    local hpp_code
    if output_file_name == "main" then
        cpp_code = cpp_translator.translate_recursive(ast, output_file_name, false, nil) -- for .cpp, global scope
        hpp_code = cpp_translator.translate_recursive(ast, output_file_name, true, nil) -- for .hpp, global scope
    else
        cpp_code = cpp_translator.translate_recursive(ast, output_file_name, false, output_file_name) -- for .cpp, module scope
        hpp_code = cpp_translator.translate_recursive(ast, output_file_name, true, output_file_name) -- for .hpp, module scope
    end

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
translate_file("examples/other_module.lua", "other_module")

-- Translate main.lua
translate_file("examples/main.lua", "main")

-- Translate test_utf8.lua
translate_file("examples/test_utf8.lua", "test_utf8")

-- Translate test_multi_assign.lua
translate_file("examples/test_multi_assign.lua", "test_multi_assign")

print("Translation complete.")