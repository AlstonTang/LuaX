local translator_module = require("src.translator")
local translator = translator_module.translate

local file_path = "/home/alston/Desktop/LuaX/tmp_test_ast.lua"
local file = io.open(file_path, "r")
if not file then
    error("Could not open file: " .. file_path)
end
local code = file:read("*all")
file:close()

local ast = translator(code)

local function print_ast(node, indent)
    indent = indent or ""
    if not node then
        print(indent .. "nil")
        return
    end
    local node_type = node.type or "UNKNOWN"
    local node_value = node.value or ""
    local node_identifier = node.identifier or ""

    local output = indent .. "Type: " .. node_type
    if node_value ~= "" then
        output = output .. ", Value: " .. tostring(node_value)
    end
    if node_identifier ~= "" then
        output = output .. ", Identifier: " .. node_identifier
    end
    print(output)

    if node.ordered_children then
        for i, child in ipairs(node.ordered_children) do
            print_ast(child, indent .. "  ")
        end
    end
end

print_ast(ast)