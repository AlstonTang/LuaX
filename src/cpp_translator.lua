-- C++ Translator Module
print("DEBUG: cpp_translator.lua is being executed.")
local CppTranslator = {}
-- Set to keep track of declared variables to handle global assignments
local declared_variables = {}
local required_modules = {}
-- Helper function to map Lua types to C++ types
local function map_lua_type_to_cpp(value)
    if type(value) == "number" then
        return "double"
    elseif type(value) == "string" then
        return "std::string"
    elseif type(value) == "boolean" then
        return "bool"
    elseif type(value) == "table" then
        return "std::shared_ptr<LuaObject>"
    else
        return "LuaValue" -- Fallback to LuaValue variant
    end
end
-- Main translation function
function CppTranslator.translate(ast_root, file_name)
    return CppTranslator.translate_recursive(ast_root, file_name, false)
end
function CppTranslator.translate_recursive(ast_root, file_name, for_header)
    declared_variables = {}
    required_modules = {}
    print("DEBUG: Tokenizing file: " .. file_name)
    local function translate_node_to_cpp(node, for_header, is_lambda)
        if not node then
            return "" -- Return empty string for nil nodes to prevent errors
        end
        local return_type = "LuaValue"
        if node.type == "Root" then
            local cpp_code = ""
                for i, child in ipairs(node.ordered_children) do
                    print("DEBUG: Root processing child " .. i .. ", type: " .. child.type)
                    cpp_code = cpp_code .. translate_node_to_cpp(child, for_header, false) .. "\n"
                end
            return cpp_code
        elseif node.type == "local_declaration" then
            local var_name = ""
            local var_type = "LuaValue" -- Default to LuaValue
            local initial_value_code = ""
            local var_node = node:find_child_by_type("variable")
            if var_node then
                var_name = var_node.identifier
                declared_variables[var_name] = true
                local initial_value_expr_node = nil
                for _, child in ipairs(node.ordered_children) do
                    if child.type ~= "variable" then
                        initial_value_expr_node = child
                        break
                    end
                end
                if initial_value_expr_node then
                    initial_value_code = " = " .. translate_node_to_cpp(initial_value_expr_node, for_header, false)
                    if initial_value_expr_node.type == "string" then var_type = "std::string" end
                    if initial_value_expr_node.type == "number" then var_type = "LuaValue" end
                    if initial_value_expr_node.type == "table_constructor" then var_type = "std::shared_ptr<LuaObject>" end
                    if initial_value_expr_node.type == "call_expression" then var_type = "LuaValue" end
                end
            end
            return var_type .. " " .. var_name .. initial_value_code .. ";"
        elseif node.type == "assignment" then
            local var_node = node.ordered_children[1]
            local value_node = node.ordered_children[2]
            if var_node and value_node then
                local value_code = translate_node_to_cpp(value_node, for_header, value_node.type == "function_declaration")
                if var_node.type == "member_expression" then
                    local base_node = var_node.ordered_children[1]
                    local member_node = var_node.ordered_children[2]
                    local translated_base = translate_node_to_cpp(base_node, for_header, false)
                    local member_name = member_node.identifier
                    -- If the base is an identifier, assume it's a LuaValue or std::shared_ptr<LuaObject>
                    -- Wrap it in LuaValue to ensure get_object can handle it
                    return "get_object(LuaValue(" .. translated_base .. "))->set(\"" .. member_name .. "\", " .. value_code .. ");"
                else
                    local var_code = translate_node_to_cpp(var_node, for_header, false)
                    local declaration_prefix = ""
                    if var_node.type == "identifier" and not declared_variables[var_node.identifier] then
                        local inferred_type = "LuaValue"
                        if value_node.type == "string" then inferred_type = "std::string" end
                        if value_node.type == "number" then inferred_type = "LuaValue" end
                        if value_node.type == "table_constructor" then inferred_type = "std::shared_ptr<LuaObject>" end
                        declaration_prefix = inferred_type .. " "
                        declared_variables[var_node.identifier] = true
                    end
                    return declaration_prefix .. var_code .. " = " .. value_code .. ";"
                end
            end
            return ""
        elseif node.type == "binary_expression" then
            local left_node = node.ordered_children[1]
            local right_node = node.ordered_children[2]
            local operator = node.value
            local translated_left = translate_node_to_cpp(left_node, for_header, false)
            local translated_right = translate_node_to_cpp(right_node, for_header, false)

            if operator == ".." then
                return "to_cpp_string(" .. translated_left .. ") + to_cpp_string(" .. translated_right .. ")"
            elseif operator == "and" then
                operator = "&&"
            elseif operator == "or" then
                operator = "||"
            elseif operator == "==" then
                return "lua_equals(" .. translated_left .. ", " .. translated_right .. ")"
            elseif operator == "~=" then
                return "lua_not_equals(" .. translated_left .. ", " .. translated_right .. ")"
            end
            return "(get_double(" .. translated_left .. ") " .. operator .. " get_double(" .. translated_right .. "))"
        elseif node.type == "if_statement" then
            local cpp_code = ""
            local first_clause = true
            for i, clause in ipairs(node.ordered_children) do
                if clause.type == "if_clause" then
                    local condition = translate_node_to_cpp(clause.ordered_children[1], for_header, false)
                    local body = translate_node_to_cpp(clause.ordered_children[2], for_header, false)
                    cpp_code = cpp_code .. "if (" .. condition .. ") {\n" .. body .. "}"
                    first_clause = false
                elseif clause.type == "elseif_clause" then
                    local condition = translate_node_to_cpp(clause.ordered_children[1], for_header, false)
                    local body = translate_node_to_cpp(clause.ordered_children[2], for_header, false)
                    cpp_code = cpp_code .. " else if (" .. condition .. ") {\n" .. body .. "}"
                elseif clause.type == "else_clause" then
                    local body = translate_node_to_cpp(clause.ordered_children[1], for_header, false)
                    cpp_code = cpp_code .. " else {\n" .. body .. "}"
                end
            end
            return cpp_code .. "\n"
        elseif node.type == "for_statement" then
            print("DEBUG: Translating for_statement")
            local var_name = node.ordered_children[1].identifier
            local start_expr = translate_node_to_cpp(node.ordered_children[2], for_header, false)
            local end_expr = translate_node_to_cpp(node.ordered_children[3], for_header, false)
            local step_expr
            local body
            if #node.ordered_children == 5 then
                step_expr = translate_node_to_cpp(node.ordered_children[4], for_header, false)
                body = translate_node_to_cpp(node.ordered_children[5], for_header, false)
            else
                step_expr = "LuaValue(1.0)"
                body = translate_node_to_cpp(node.ordered_children[4], for_header, false)
            end
            declared_variables[var_name] = true
            return "for (LuaValue " .. var_name .. " = " .. start_expr .. "; get_double(" .. var_name .. ") <= get_double(" .. end_expr .. "); " .. var_name .. " = LuaValue(get_double(" .. var_name .. ") + get_double(" .. step_expr .. "))) {\n" .. body .. "}"
        elseif node.type == "while_statement" then
            print("DEBUG: Translating while_statement")
            local condition = translate_node_to_cpp(node.ordered_children[1], for_header, false)
            local body = translate_node_to_cpp(node.ordered_children[2], for_header, false)
            return "while (" .. condition .. ") {\n" .. body .. "}"
        elseif node.type == "call_expression" then
            local func_node = node.ordered_children[1]
            if func_node.type == "member_expression" and func_node.ordered_children[1].identifier == "string" and func_node.ordered_children[2].identifier == "match" then
                local base_string_node = node.ordered_children[2]
                local pattern_node = node.ordered_children[3]
                local base_string_code = translate_node_to_cpp(base_string_node, for_header, false)
                local pattern_code = translate_node_to_cpp(pattern_node, for_header, false)
                return "std::regex_search(" .. base_string_code .. ", std::regex(" .. pattern_code .. "))"
            elseif func_node.type == "member_expression" and func_node.ordered_children[1].identifier == "string" and func_node.ordered_children[2].identifier == "find" then
                local base_string_node = node.ordered_children[2]
                local pattern_node = node.ordered_children[3]
                local base_string_code = translate_node_to_cpp(base_string_node, for_header, false)
                local pattern_code = translate_node_to_cpp(pattern_node, for_header, false)
                return "LuaValue(" .. base_string_code .. ".find(" .. pattern_code .. ") != std::string::npos ? static_cast<double>(" .. base_string_code .. ".find(" .. pattern_code .. ") + 1) : 0.0)"
            elseif func_node.type == "member_expression" and func_node.ordered_children[1].identifier == "string" and func_node.ordered_children[2].identifier == "gsub" then
                local base_string_node = node.ordered_children[2]
                local pattern_node = node.ordered_children[3]
                local replacement_node = node.ordered_children[4]
                local base_string_code = translate_node_to_cpp(base_string_node, for_header, false)
                local pattern_code = translate_node_to_cpp(pattern_node, for_header, false)
                local replacement_code = translate_node_to_cpp(replacement_node, for_header, false)
                return "std::regex_replace(" .. base_string_code .. ", std::regex(" .. pattern_code .. "), " .. replacement_code .. ")"
            elseif func_node.type == "identifier" and func_node.identifier == "print" then
                local cpp_code = ""
                for i, arg_node in ipairs(node.ordered_children) do
                    if i > 1 then
                        cpp_code = cpp_code .. "print_value(" .. translate_node_to_cpp(arg_node, for_header, false) .. ");"
                        if i < #node.ordered_children then
                            cpp_code = cpp_code .. "std::cout << \"\\t\";" -- Add tab between arguments
                        end
                    end
                end
                cpp_code = cpp_code .. "std::cout << std::endl;"
                return cpp_code
            elseif func_node.type == "identifier" and func_node.identifier == "require" then
                local module_name_node = node.ordered_children[2]
                if module_name_node and module_name_node.type == "string" then
                    local module_name = module_name_node.value
                    required_modules[module_name] = true
                    return module_name .. "::load()"
                end
                return "/* require call with non-string argument */"
            elseif func_node.type == "identifier" and func_node.identifier == "setmetatable" then
                local table_node = node.ordered_children[2]
                local metatable_node = node.ordered_children[3]
                local translated_table = translate_node_to_cpp(table_node, for_header, false)
                local translated_metatable = translate_node_to_cpp(metatable_node, for_header, false)
                return "get_object(" .. translated_table .. ")->set_metatable(get_object(" .. translated_metatable .. "));"
            elseif func_node.type == "member_expression" then
                local base_node = func_node.ordered_children[1]
                local member_node = func_node.ordered_children[2]
                local member_name = member_node.identifier
                local translated_base
                if base_node.type == "identifier" and (base_node.identifier == "math" or base_node.identifier == "string" or base_node.identifier == "table" or base_node.identifier == "os" or base_node.identifier == "io" or base_node.identifier == "package") then
                    translated_base = "get_object(_G->get(\"" .. base_node.identifier .. "\"))"
                else
                    translated_base = "get_object(" .. translate_node_to_cpp(base_node, for_header, false) .. ")"
                end

                local args_table = "std::make_shared<LuaObject>()"
                local args_code = "( [&]() { auto temp_args = " .. args_table .. ";\n"
                for i, arg_node in ipairs(node.ordered_children) do
                    if i > 1 then
                        args_code = args_code .. "temp_args->set(\"" .. tostring(i-1) .. "\", " .. translate_node_to_cpp(arg_node, for_header, false) .. ");\n"
                    end
                end
                args_code = args_code .. "return temp_args; } )()"

                return "std::get<std::shared_ptr<LuaFunctionWrapper>>(".. translated_base .. "->get(\"" .. member_name .. "\"))->func(" .. args_code .. ")"

            elseif func_node.type == "identifier" and (func_node.identifier == "tonumber" or func_node.identifier == "tostring" or func_node.identifier == "type" or func_node.identifier == "getmetatable" or func_node.identifier == "error" or func_node.identifier == "pcall") then
                local translated_func = "get_object(_G)->get(\"" .. func_node.identifier .. "\")"
                local args_table = "std::make_shared<LuaObject>()"
                local args_code = "( [&]() { auto temp_args = " .. args_table .. ";\n"
                for i, arg_node in ipairs(node.ordered_children) do
                    if i > 1 then
                        args_code = args_code .. "temp_args->set(\"" .. tostring(i-1) .. "\", " .. translate_node_to_cpp(arg_node, for_header, false) .. ");\n"
                    end
                end
                args_code = args_code .. "return temp_args; } )()"
                return "std::get<std::shared_ptr<LuaFunctionWrapper>>(" .. translated_func .. ")->func(" .. args_code .. ")"
            elseif func_node.type == "identifier" and func_node.identifier == "rawget" then
                local table_node = node.ordered_children[2]
                local key_node = node.ordered_children[3]
                local translated_table = translate_node_to_cpp(table_node, for_header, false)
                local translated_key = translate_node_to_cpp(key_node, for_header, false)
                return "rawget(get_object(" .. translated_table .. "), " .. translated_key .. ")"
            elseif func_node.type == "identifier" and func_node.identifier == "rawset" then
                local table_node = node.ordered_children[2]
                local key_node = node.ordered_children[3]
                local value_node = node.ordered_children[4]
                local translated_table = translate_node_to_cpp(table_node, for_header, false)
                local translated_key = translate_node_to_cpp(key_node, for_header, false)
                local translated_value = translate_node_to_cpp(value_node, for_header, false)
                return "get_object(" .. translated_table .. ")->properties[std::get<std::string>(" .. translated_key .. ")] = " .. translated_value .. ";"
            else
                local translated_func = translate_node_to_cpp(func_node, for_header, false)
                local args_table = "std::make_shared<LuaObject>()"
                local args_code = "( [&]() { auto temp_args = " .. args_table .. ";\n"
                for i, arg_node in ipairs(node.ordered_children) do
                    if i > 1 then
                        args_code = args_code .. "temp_args->set(\"" .. tostring(i-1) .. "\", " .. translate_node_to_cpp(arg_node, for_header, false) .. ");\n"
                    end
                end
                args_code = args_code .. "return temp_args; } )()"
                return "std::get<std::shared_ptr<LuaFunctionWrapper>>(" .. translated_func .. ")->func(" .. args_code .. ")"
            end
        elseif node.type == "member_expression" then
            local base_node = node.ordered_children[1]
            local member_node = node.ordered_children[2]
            local base_code = translate_node_to_cpp(base_node, for_header, false)
            if base_node.type == "identifier" and (base_node.identifier == "math" or base_node.identifier == "string" or base_node.identifier == "table" or base_node.identifier == "os" or base_node.identifier == "io" or base_node.identifier == "package") then
                return "get_object(_G->get(\"" .. base_node.identifier .. "\"))->get(\"" .. member_node.identifier .. "\")"
            else
                return "get_object(" .. base_code .. ")->get(\"" .. member_node.identifier .. "\")"
            end
        elseif node.type == "string" then
            local escaped_value = node.value:gsub('"', '\\"')
            return "\"" .. escaped_value .. "\""
        elseif node.type == "number" then
            local num_str = tostring(node.value)
            if string.find(num_str, "%.") then
                return "LuaValue(" .. num_str .. ")"
            else
                return "LuaValue(" .. num_str .. ".0)"
            end
        elseif node.type == "identifier" then
            if node.identifier == "_VERSION" then
                return "get_object(_G)->get(\"_VERSION\")"
            elseif node.identifier == "nil" then
                return "std::monostate{}"
            else
                return node.identifier
            end
        elseif node.type == "table_constructor" then
            local cpp_code = "std::make_shared<LuaObject>()"
            local fields = node:get_all_children_of_type("table_field")
            local list_index = 1
            local temp_table_var = "temp_table_" .. tostring(math.random(10000))
            local construction_code = "auto " .. temp_table_var .. " = " .. cpp_code .. ";\n"
            for i, field_node in ipairs(fields) do
                local key_part
                local value_part
                local key_node = field_node.ordered_children[1]
                local value_node = field_node.ordered_children[2]
                if key_node and value_node then -- Record-style field (key = value)
                    key_part = "\"" .. key_node.identifier .. "\""
                    if value_node.type == "function_declaration" then
                        value_part = translate_node_to_cpp(value_node, for_header, true)
                    else
                        value_part = translate_node_to_cpp(value_node, for_header, false)
                    end
                else -- List-style field (value only)
                    value_node = field_node.ordered_children[1]
                    if value_node.type == "function_declaration" then
                        key_part = "\"" .. value_node.identifier .. "\""
                        value_part = translate_node_to_cpp(value_node, for_header, true)
                    else
                        key_part = "\"" .. tostring(list_index) .. "\""
                        value_part = translate_node_to_cpp(value_node, for_header, false)
                        list_index = list_index + 1
                    end
                end
                if key_part and value_part then
                    construction_code = construction_code .. temp_table_var .. "->set(" .. key_part .. ", " .. value_part .. ");\n"
                end
            end
        return "( [&]() { " .. construction_code .. " return " .. temp_table_var .. "; } )()"
    elseif node.type == "method_call_expression" then
        local base_node = node.ordered_children[1]
        local method_node = node.ordered_children[2]
        local cpp_code = translate_node_to_cpp(base_node, for_header, false) .. "->" .. method_node.identifier .. "("
        for i, arg_node in ipairs(node.ordered_children) do
            if i > 2 then
                cpp_code = cpp_code .. translate_node_to_cpp(arg_node, for_header, false)
                if i < #node.ordered_children then
                    cpp_code = cpp_code .. ", "
                end
            end
        end
        return cpp_code .. ")"
    elseif node.type == "expression_statement" then
        local expr_node = node.ordered_children[1]
        if expr_node then
            local translated_expr = translate_node_to_cpp(expr_node, for_header, false)
            return translated_expr .. ";"
        end
        return ""
                    elseif node.type == "function_declaration" or node.type == "method_declaration" then
                        local func_name = node.identifier
                        if node.method_name then
                            func_name = func_name .. "_" .. node.method_name
                        end
                        local params_node = node.ordered_children[1]
                        local body_node = node.ordered_children[2]
                        local return_type = "LuaValue"

                        local params_extraction = ""
                        for i, param_node in ipairs(params_node.ordered_children) do
                            params_extraction = params_extraction .. "    LuaValue " .. param_node.identifier .. " = args->get(\"" .. tostring(i) .. "\");\n"
                        end

                        local lambda_body = translate_node_to_cpp(body_node, for_header, false)
                        local has_explicit_return = false
                        for _, child_statement in ipairs(body_node.ordered_children) do
                            if child_statement.type == "return_statement" then
                                has_explicit_return = true
                                break
                            end
                        end
                        if return_type == "LuaValue" and not has_explicit_return then
                            lambda_body = lambda_body .. "return std::monostate{};\n"
                        end

                        local lambda_code = "std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> " .. return_type .. " {\n" .. params_extraction .. lambda_body .. "})"

                        if is_lambda then
                            return lambda_code
                        end

                        if node.method_name ~= nil then
                            return "get_object(LuaValue(" .. node.identifier .. "))->set(\"" .. node.method_name .. "\", " .. lambda_code .. ");"
                        elseif node.identifier ~= nil then
                            return "local " .. func_name .. " = " .. lambda_code .. ";"
                        else
                            return lambda_code
                        end

        elseif node.type == "return_statement" then
            local return_expr_node = node.ordered_children[1]
            if return_expr_node then
                if return_expr_node.type == "identifier" then
                    return "return " .. translate_node_to_cpp(return_expr_node, for_header, false) .. ";"
                else
                    return "return " .. translate_node_to_cpp(return_expr_node, for_header, false) .. ";"
                end
            else
                return "return std::make_shared<LuaObject>();"
            end
        elseif node.type == "identifier" and node.identifier == "__index" then
            return "\"__index\""
        elseif node.type == "unary_expression" then
            local operator = node.value
            local operand_node = node.ordered_children[1]
            return operator .. translate_node_to_cpp(operand_node, for_header, false)
        elseif node.type == "block" then
            local block_cpp_code = ""
            for _, child in ipairs(node.ordered_children) do
                block_cpp_code = block_cpp_code .. "    " .. translate_node_to_cpp(child, for_header, false) .. "\n"
            end
            return block_cpp_code
        else
            print("WARNING: Unhandled node type: " .. node.type)
            return "/* UNHANDLED_NODE_TYPE: " .. node.type .. " */"
        end
    end
    
    local generated_code = translate_node_to_cpp(ast_root, for_header, false)
    
    local header = "#include <iostream>\n#include <vector>\n#include <string>\n#include <map>\n#include <memory>\n#include <variant>\n#include <regex>\n#include <functional>\n#include \"lua_object.hpp\"\n\n"
    
    for module_name, _ in pairs(required_modules) do
        header = header .. "#include \"" .. module_name .. ".hpp\"\n"
    end
    header = header .. "#include \"math.hpp\"\n"
    header = header .. "#include \"string.hpp\"\n"
    header = header .. "#include \"table.hpp\"\n"
    header = header .. "#include \"os.hpp\"\n"
    header = header .. "#include \"io.hpp\"\n"
    header = header .. "#include \"package.hpp\"\n"
    
    if file_name == "main" then
        local main_function_start = "int main() {\n" ..
                                    "    _G->set(\"math\", create_math_library());\n" ..
                                    "    _G->set(\"string\", create_string_library());\n" ..
                                    "    _G->set(\"table\", create_table_library());\n" ..
                                    "    _G->set(\"os\", create_os_library());\n" ..
                                    "    _G->set(\"io\", create_io_library());\n" ..
                                    "    _G->set(\"package\", create_package_library());\n" ..
                                    "    _G->set(\"_VERSION\", LuaValue(std::string(\"Lua 5.4\")));\n" ..
                                    "    _G->set(\"tonumber\", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {\n" ..
                                    "        // tonumber implementation\n" ..
                                    "        LuaValue val = args->get(\"1\");\n" ..
                                    "        if (std::holds_alternative<double>(val)) {\n" ..
                                    "            return val;\n" ..
                                    "        } else if (std::holds_alternative<std::string>(val)) {\n" ..
                                    "            std::string s = std::get<std::string>(val);\n" ..
                                    "            try {\n" ..
                                    "                // Check if the string contains only digits and an optional decimal point\n" ..
                                    "                if (s.find_first_not_of(\"0123456789.\") == std::string::npos) {\n" ..
                                    "                    return std::stod(s);\n" ..
                                    "                }\n" ..
                                    "            } catch (...) {\n" ..
                                    "                // Fall through to return nil\n" ..
                                    "            }\n" ..
                                    "        }\n" ..
                                    "        return std::monostate{}; // nil\n" ..
                                    "    }));\n" ..
                                    "    _G->set(\"tostring\", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {\n" ..
                                    "        // tostring implementation\n" ..
                                    "        return to_cpp_string(args->get(\"1\"));\n" ..
                                    "    }));\n" ..
                                    "    _G->set(\"type\", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {\n" ..
                                    "        // type implementation\n" ..
                                    "        LuaValue val = args->get(\"1\");\n" ..
                                    "        if (std::holds_alternative<std::monostate>(val)) return \"nil\";\n" ..
                                    "        if (std::holds_alternative<bool>(val)) return \"boolean\";\n" ..
                                    "        if (std::holds_alternative<double>(val)) return \"number\";\n" ..
                                    "        if (std::holds_alternative<std::string>(val)) return \"string\";\n" ..
                                    "        if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) return \"table\";\n" ..
                                    "        if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(val)) return \"function\";\n" ..
                                    "        return \"unknown\";\n" ..
                                    "    }));\n" ..
                                    "    _G->set(\"getmetatable\", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {\n" ..
                                    "        // getmetatable implementation\n" ..
                                    "        LuaValue val = args->get(\"1\");\n" ..
                                    "        if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) {\n" ..
                                    "            auto obj = std::get<std::shared_ptr<LuaObject>>(val);\n" ..
                                    "            if (obj->metatable) {\n" ..
                                    "                return obj->metatable;\n" ..
                                    "            }\n" ..
                                    "        }\n" ..
                                    "        return std::monostate{}; // nil\n" ..
                                    "    }));\n" ..
                                    "    _G->set(\"error\", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {\n" ..
                                    "        // error implementation\n" ..
                                    "        LuaValue message = args->get(\"1\");\n" ..
                                    "        try {\n" ..
                                    "            throw std::runtime_error(to_cpp_string(message));\n" ..
                                    "        } catch (const std::exception& e) {\n" ..
                                    "            std::cerr << \"Error function caught exception: \" << e.what() << std::endl;\n" ..
                                    "            throw;\n" ..
                                    "        }\n" ..
                                    "        return std::monostate{}; // Should not be reached\n" ..
                                    "    }));\n" ..
                                    "    _G->set(\"pcall\", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {\n" ..
                                    "        // pcall implementation\n" ..
                                    "        LuaValue func_to_call = args->get(\"1\");\n" ..
                                    "        if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(func_to_call)) {\n" ..
                                    "            auto callable_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(func_to_call);\n" ..
                                    "            auto func_args = std::make_shared<LuaObject>();\n" ..
                                    "            for (int i = 2; ; ++i) {\n" ..
                                    "                LuaValue arg = args->get(std::to_string(i));\n" ..
                                    "                if (std::holds_alternative<std::monostate>(arg)) break;\n" ..
                                    "                func_args->set(std::to_string(i - 1), arg);\n" ..
                                    "            }\n" ..
                                    "            try {\n" ..
                                    "                LuaValue result = callable_func->func(func_args);\n" ..
                                    "                auto results = std::make_shared<LuaObject>();\n" ..
                                    "                results->set(\"1\", true);\n" ..
                                    "                results->set(\"2\", result);\n" ..
                                    "                return results;\n" ..
                                    "            } catch (...) {\n" ..
                                    "                auto results = std::make_shared<LuaObject>();\n" ..
                                    "                results->set(\"1\", false);\n" ..
                                    "                results->set(\"2\", \"An unknown error occurred\");\n" ..
                                    "                return results;\n" ..
                                    "            }\n" ..
                                    "        }\n" ..
                                    "        return false; // Not a callable function\n" ..
                                    "    }));\n"
        local main_function_end = "\n    return 0;\n}"
        return header .. main_function_start .. generated_code .. main_function_end
    else -- Module generation
        local namespace_start = "namespace " .. file_name .. " {\n"
        local namespace_end = "\n} // namespace " .. file_name .. "\n"
        if for_header then -- Generate .hpp content
            local hpp_header = "#pragma once\n#include \"lua_object.hpp\"\n\n"
            local hpp_load_function_declaration = "std::shared_ptr<LuaObject> load();\n"
            return hpp_header .. namespace_start .. hpp_load_function_declaration .. namespace_end
        else -- Generate .cpp content
            local cpp_header = "#include \"" .. file_name .. ".hpp\"\n"
            local load_function_body = ""
            local module_identifier = ""

            -- Find the module's local declaration
            for _, child in ipairs(ast_root.ordered_children) do
                if child.type == "local_declaration" and child.ordered_children[1].type == "variable" and child.ordered_children[2].type == "table_constructor" then
                    module_identifier = child.ordered_children[1].identifier
                    load_function_body = load_function_body .. "std::shared_ptr<LuaObject> " .. module_identifier .. " = std::make_shared<LuaObject>();\n"
                    break -- Assuming only one module declaration per file
                end
            end

            for _, child in ipairs(ast_root.ordered_children) do
                if child.type ~= "local_declaration" and child.type ~= "return_statement" then
                    load_function_body = load_function_body .. translate_node_to_cpp(child, false, false) .. "\n"
                end
            end
            load_function_body = load_function_body .. "return " .. module_identifier .. ";\n"

            local load_function_definition = "std::shared_ptr<LuaObject> load() {\n" .. load_function_body .. "}\n"
            return header .. cpp_header .. namespace_start .. load_function_definition .. namespace_end
        end
    end
end

return CppTranslator