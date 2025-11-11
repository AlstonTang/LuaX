-- C++ Translator Module

local CppTranslator = {}

-- Helper function to map Lua types to C++ types (basic)
local function map_lua_type_to_cpp(value)
    if type(value) == "number" then
        -- Could be int or float, default to double for generality
        return "double"
    elseif type(value) == "string" then
        return "std::string"
    elseif type(value) == "boolean" then
        return "bool"
    elseif type(value) == "table" then
        -- Represent tables as std::vector for simplicity, or std::map if keys are identifiers
        -- This is a simplification and might need more sophisticated handling
        return "std::vector<std::string>" -- Placeholder, needs better type inference
    else
        return "auto" -- Fallback
    end
end

-- Recursive function to translate AST nodes to C++ code
local function translate_node_to_cpp(node)
    local cpp_code = ""

    if node.type == "Root" then
        for _, child in ipairs(node.ordered_children) do
            cpp_code = cpp_code .. translate_node_to_cpp(child) .. "\n"
        end
    elseif node.type == "local_declaration" then
        local var_name = node.identifier
        local var_type = "auto" -- Default to auto
        local initial_value_code = ""

        -- Try to find the variable node and its initial value
        local var_node = node:find_child_by_type("variable")
        if var_node then
            var_name = var_node.identifier
            -- Check for an initial assignment
            local assignment_node = node:find_child_by_type("assignment")
            if assignment_node then
                local value_node = assignment_node:find_child_by_type("binary_expression") or 
                                   assignment_node:find_child_by_type("call_expression") or 
                                   assignment_node:find_child_by_type("member_expression") or 
                                   assignment_node:find_child_by_type("table_constructor") or 
                                   assignment_node:find_child_by_type("number") or 
                                   assignment_node:find_child_by_type("string") or 
                                   assignment_node:find_child_by_type("identifier")
                if value_node then
                    initial_value_code = " = " .. translate_node_to_cpp(value_node)
                    -- Attempt to infer type from the initial value
                    -- This is a very basic inference and needs improvement
                    if value_node.type == "string" then var_type = "std::string" end
                    if value_node.type == "number" then var_type = "double" end
                    -- Add more type inference here for tables, calls, etc.
                end
            end
        end
        
        cpp_code = cpp_code .. var_type .. " " .. var_name .. initial_value_code .. ";"

    elseif node.type == "assignment" then
        local var_node = node:find_child_by_type("variable")
        local value_node = node:find_child_by_type("binary_expression") or 
                           node:find_child_by_type("call_expression") or 
                           node:find_child_by_type("member_expression") or 
                           node:find_child_by_type("table_constructor") or 
                           node:find_child_by_type("number") or 
                           node:find_child_by_type("string") or 
                           node:find_child_by_type("identifier")

        if var_node and value_node then
            cpp_code = cpp_code .. var_node.identifier .. " = " .. translate_node_to_cpp(value_node) .. ";"
        end

    elseif node.type == "binary_expression" then
        local left_node = node.ordered_children[1]
        local right_node = node.ordered_children[2]
        cpp_code = cpp_code .. translate_node_to_cpp(left_node) .. " " .. node.value .. " " .. translate_node_to_cpp(right_node)

    elseif node.type == "call_expression" then
        local func_node = node.ordered_children[1]
        cpp_code = cpp_code .. translate_node_to_cpp(func_node) .. "("
        for i, arg_node in ipairs(node.ordered_children) do
            if i > 1 then -- Skip the function node itself
                cpp_code = cpp_code .. translate_node_to_cpp(arg_node)
                if i < #node.ordered_children then
                    cpp_code = cpp_code .. ", "
                end
            end
        end
        cpp_code = cpp_code .. ")"

    elseif node.type == "member_expression" then
        local base_node = node.ordered_children[1]
        local member_node = node.ordered_children[2]
        cpp_code = cpp_code .. translate_node_to_cpp(base_node) .. "." .. member_node.identifier

    elseif node.type == "string" then
        -- C++ strings need to be quoted, escape internal quotes if necessary (basic)
        local escaped_value = node.value:gsub('"', '\\"')
        cpp_code = cpp_code .. "\"" .. escaped_value .. "\""

    elseif node.type == "number" then
        cpp_code = cpp_code .. tostring(node.value)

    elseif node.type == "identifier" then
        cpp_code = cpp_code .. node.identifier
        
    elseif node.type == "table_constructor" then
        -- Basic translation for table constructors to std::vector
        cpp_code = cpp_code .. "std::vector<std::string> { " -- Placeholder type
        local fields = node:get_all_children_of_type("table_field")
        for i, field_node in ipairs(fields) do
            local value_node = field_node:find_child_by_type("binary_expression") or 
                               field_node:find_child_by_type("call_expression") or 
                               field_node:find_child_by_type("member_expression") or 
                               field_node:find_child_by_type("table_constructor") or 
                               field_node:find_child_by_type("number") or 
                               field_node:find_child_by_type("string") or 
                               field_node:find_child_by_type("identifier")
            if value_node then
                cpp_code = cpp_code .. translate_node_to_cpp(value_node)
                if i < #fields then
                    cpp_code = cpp_code .. ", "
                end
            end
        end
        cpp_code = cpp_code .. " }"

    -- Add more node types here as needed (e.g., method_call_expression, table_field, etc.)
    end

    return cpp_code
end

-- Main translation function
function CppTranslator.translate(ast_root)
    local header = "#include <iostream>\n#include <vector>\n#include <string>\n#include <map>\n\n-- Forward declarations for functions would go here if needed\n\n"
    local main_function_start = "int main() {\n    -- Code generated from Lua script\n"
    local main_function_end = "\n    return 0;\n}"

    local generated_code = translate_node_to_cpp(ast_root)

    return header .. main_function_start .. generated_code .. main_function_end
end

return CppTranslator
