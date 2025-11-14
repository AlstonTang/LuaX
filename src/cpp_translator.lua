-- C++ Translator Module
local CppTranslator = {}
-- Set to keep track of declared variables to handle global assignments
local declared_variables = {}
local required_modules = {}
local module_global_vars = {} -- New table to store global variables declared in the current module

-- Helper function to map Lua types to C++ types
local function map_lua_type_to_cpp(value)
    if type(value) == "number" then
        return "double"
    elseif type(value) == "integer" then
        return "long long"
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
function CppTranslator.translate(ast_root, file_name, is_main_script) -- ADDED is_main_script parameter
    declared_variables = {}
    required_modules = {}
    module_global_vars = {} -- Reset for each translation run
    -- Pass is_main_script down
    return CppTranslator.translate_recursive(ast_root, file_name, false, nil, is_main_script)
end

-- Modified Signature to include is_main_script
function CppTranslator.translate_recursive(ast_root, file_name, for_header, current_module_object_name, is_main_script)
    local function translate_node_to_cpp(node, for_header, is_lambda, current_module_object_name)
        if not node then
            return "" -- Return empty string for nil nodes to prevent errors
        end
        if not node.type then
            print("ERROR: Node has no type field:", node)
            return "/* ERROR: Node with no type */"
        end
        local return_type = "LuaValue"

        -- Helper function to get the C++ code for a single LuaValue, extracting from vector if necessary
        local function get_single_lua_value_cpp_code(node_to_translate, for_header_arg, is_lambda_arg, current_module_object_name_arg)
            local translated_code = translate_node_to_cpp(node_to_translate, for_header_arg, is_lambda_arg, current_module_object_name_arg)
            if node_to_translate.type == "call_expression" then
                return translated_code .. "[0]"
            else
                return translated_code
            end
        end        if node.type == "Root" then
            local cpp_code = ""
                for i, child in ipairs(node.ordered_children) do
                    cpp_code = cpp_code .. translate_node_to_cpp(child, for_header, false, current_module_object_name) .. "\n"
                end
            return cpp_code
        elseif node.type == "local_declaration" then
            local var_list_node = node.ordered_children[1]
            local expr_list_node = node.ordered_children[2]
            local cpp_code = ""

            local num_vars = #var_list_node.ordered_children
            local num_exprs = expr_list_node and #expr_list_node.ordered_children or 0
            local cpp_code = ""

            local function_call_results_var = "func_results_" .. tostring(math.random(10000))
            local has_function_call_expr = false
            local first_call_expr_node = nil

            -- First pass: check if any expression is a function call
            for i = 1, num_exprs do
                local expr_node = expr_list_node.ordered_children[i]
                if expr_node.type == "call_expression" then
                    has_function_call_expr = true
                    first_call_expr_node = expr_node
                    break
                end
            end

            if has_function_call_expr and first_call_expr_node then
                -- If there's a function call, evaluate it once and store results
                cpp_code = cpp_code .. "std::vector<LuaValue> " .. function_call_results_var .. " = " .. translate_node_to_cpp(first_call_expr_node, for_header, false, current_module_object_name) .. ";\n"
            end

            for i = 1, num_vars do
                local var_node = var_list_node.ordered_children[i]
                local var_name = var_node.identifier
                declared_variables[var_name] = true
                local var_type = "LuaValue" -- Default type

                local initial_value_code = "std::monostate{}" -- Default to nil

                if i <= num_exprs then
                    local expr_node = expr_list_node.ordered_children[i]
                    if expr_node.type == "call_expression" then
                        -- For function calls, take from the results vector
                        initial_value_code = "(" .. function_call_results_var .. ".size() > " .. (i - 1) .. " ? " .. function_call_results_var .. "[" .. (i - 1) .. "] : std::monostate{})"
                    else
                        initial_value_code = translate_node_to_cpp(expr_node, for_header, false, current_module_object_name)

                    end
                elseif has_function_call_expr then
                    initial_value_code = "(" .. function_call_results_var .. ".size() > " .. (i - 1) .. " ? " .. function_call_results_var .. "[" .. (i - 1) .. "] : std::monostate{})"
                end
                cpp_code = cpp_code .. var_type .. " " .. var_name .. " = " .. initial_value_code .. ";\n"
            end
            return cpp_code
        elseif node.type == "assignment" then
            local var_list_node = node.ordered_children[1]
            local expr_list_node = node.ordered_children[2]
            local cpp_code = ""

            local num_vars = #var_list_node.ordered_children
            local num_exprs = #expr_list_node.ordered_children

            local function_call_results_var = "func_results_" .. tostring(os.time()) .. "_" .. tostring(math.random(10000))
            local has_function_call_expr = false
            local first_call_expr_node = nil

            -- First pass: check if any expression is a function call
            for i = 1, num_exprs do
                local expr_node = expr_list_node.ordered_children[i]
                if expr_node.type == "call_expression" then
                    has_function_call_expr = true
                    first_call_expr_node = expr_node
                    break
                end
            end

            if has_function_call_expr and first_call_expr_node then
                -- If there's a function call, evaluate it once and store results
                cpp_code = cpp_code .. "std::vector<LuaValue> " .. function_call_results_var .. " = " .. translate_node_to_cpp(first_call_expr_node, for_header, false, current_module_object_name) .. ";\n"
            end

            for i = 1, num_vars do
                local var_node = var_list_node.ordered_children[i]
                local value_code = "std::monostate{}" -- Default to nil

                if i <= num_exprs then
                    local expr_node = expr_list_node.ordered_children[i]
                    if expr_node.type == "call_expression" then
                        -- For function calls, take from the results vector
                        value_code = "(" .. function_call_results_var .. ".size() > " .. (i - 1) .. " ? " .. function_call_results_var .. "[" .. (i - 1) .. "] : std::monostate{})"
                    else
                        value_code = translate_node_to_cpp(expr_node, for_header, false, current_module_object_name)
                    end
                elseif has_function_call_expr then
                    value_code = "(" .. function_call_results_var .. ".size() > " .. (i - 1) .. " ? " .. function_call_results_var .. "[" .. (i - 1) .. "] : std::monostate{})"
                end

                if var_node.type == "member_expression" then
                    local base_node = var_node.ordered_children[1]
                    local member_node = var_node.ordered_children[2]
                    local translated_base = translate_node_to_cpp(base_node, for_header, false, current_module_object_name)
                    local member_name = member_node.identifier
                    cpp_code = cpp_code .. "get_object(" .. translated_base .. ")->set(\"" .. member_name .. "\", " .. value_code .. ");\n"
                elseif var_node.type == "table_index_expression" then
                    local base_node = var_node.ordered_children[1]
                    local index_node = var_node.ordered_children[2]
                    local translated_base = translate_node_to_cpp(base_node, for_header, false, current_module_object_name)
                    local translated_index = translate_node_to_cpp(index_node, for_header, false, current_module_object_name)
                    cpp_code = cpp_code .. "get_object(" .. translated_base .. ")->set_item(" .. translated_index .. ", " .. value_code .. ");\n"
                else
                    local var_code = translate_node_to_cpp(var_node, for_header, false, current_module_object_name)
                    local declaration_prefix = ""
                    if var_node.type == "identifier" and not declared_variables[var_node.identifier] then
                        if is_main_script then -- CHECK: Use is_main_script flag
                            declaration_prefix = "LuaValue "
                        else
                            -- For modules, global variables need to be declared in the module's scope
                            module_global_vars[var_node.identifier] = true
                        end
                        declared_variables[var_node.identifier] = true
                    end
                    cpp_code = cpp_code .. declaration_prefix .. var_code .. " = " .. value_code .. ";\n"
                end
            end
            return cpp_code
        elseif node.type == "binary_expression" then
            local left_node = node.ordered_children[1]
            local right_node = node.ordered_children[2]
            local operator = node.value
            local translated_left = translate_node_to_cpp(left_node, for_header, false, current_module_object_name)
            local translated_right = translate_node_to_cpp(right_node, for_header, false, current_module_object_name)

            if operator == ".." then
                return "to_cpp_string(" .. translated_left .. ") + to_cpp_string(" .. translated_right .. ")"
            elseif operator == "and" then
                -- Lua: A and B -> if A is falsey, return A, else return B.
                return "(is_lua_truthy(" .. translated_left .. ") ? " .. translated_right .. " : " .. translated_left .. ")"
            elseif operator == "or" then
                -- Lua: A or B -> if A is truthy, return A, else return B.
                return "(is_lua_truthy(" .. translated_left .. ") ? " .. translated_left .. " : " .. translated_right .. ")"
            elseif operator == "==" then
                return "lua_equals(" .. translated_left .. ", " .. translated_right .. ")"
            elseif operator == "~=" then
                return "lua_not_equals(" .. translated_left .. ", " .. translated_right .. ")"
            end
            -- Arithmetic/Comparison operators return LuaValue (number or boolean)
            return "(get_double(" .. get_single_lua_value_cpp_code(left_node, for_header, false, current_module_object_name) .. ") " .. operator .. " get_double(" .. get_single_lua_value_cpp_code(right_node, for_header, false, current_module_object_name) .. "))"
        elseif node.type == "if_statement" then
            local cpp_code = ""
            local first_clause = true
            for i, clause in ipairs(node.ordered_children) do
                if clause.type == "if_clause" then
                    local condition = translate_node_to_cpp(clause.ordered_children[1], for_header, false, current_module_object_name)
                    local body = translate_node_to_cpp(clause.ordered_children[2], for_header, false, current_module_object_name)
                    -- FIX: Wrap condition in is_lua_truthy
                    cpp_code = cpp_code .. "if (is_lua_truthy(" .. condition .. ")) {\n" .. body .. "}"
                    first_clause = false
                elseif clause.type == "elseif_clause" then
                    local condition = translate_node_to_cpp(clause.ordered_children[1], for_header, false, current_module_object_name)
                    local body = translate_node_to_cpp(clause.ordered_children[2], for_header, false, current_module_object_name)
                    -- FIX: Wrap condition in is_lua_truthy
                    cpp_code = cpp_code .. " else if (is_lua_truthy(" .. condition .. ")) {\n" .. body .. "}"
                elseif clause.type == "else_clause" then
                    local body = translate_node_to_cpp(clause.ordered_children[1], for_header, false, current_module_object_name)
                    cpp_code = cpp_code .. " else {\n" .. body .. "}"
                end
            end
            return cpp_code .. "\n"
        elseif node.type == "for_numeric_statement" then
            local var_name = node.ordered_children[1].identifier
            local var_type = node.ordered_children[1].type
            local start_expr_code = get_single_lua_value_cpp_code(node.ordered_children[2], for_header, false, current_module_object_name)
            local end_expr_code = get_single_lua_value_cpp_code(node.ordered_children[3], for_header, false, current_module_object_name)
            local end_expr_type = node.ordered_children[3].type
            local step_expr_code
            local body_node
            if #node.ordered_children == 5 then
                step_expr_code = get_single_lua_value_cpp_code(node.ordered_children[4], for_header, false, current_module_object_name)
                body_node = node.ordered_children[5]
            else
                step_expr_code = "LuaValue(1.0)"
                body_node = node.ordered_children[4]
            end
            local loop_condition = ""
            if step_expr_code == "LuaValue(1.0)" or step_expr_code == "LuaValue(1)" then -- Default step is 1.0, assume positive
                if var_type == "integer" and end_expr_type == "integer" then
                    loop_condition = "get_long_long(" .. var_name .. ") <= get_long_long(" .. end_expr_code .. ")"
                else
                    loop_condition = "get_double(" .. var_name .. ") <= get_double(" .. end_expr_code .. ")"
                end
            else
                -- Determine if step is positive or negative at runtime
                loop_condition = "(get_double(" .. step_expr_code .. ") >= 0 ? get_double(" .. var_name .. ") <= get_double(" .. end_expr_code .. ") : get_double(" .. var_name .. ") >= get_double(" .. end_expr_code .. "))"
            end
            declared_variables[var_name] = true
            return "for (LuaValue " .. var_name .. " = " .. start_expr_code .. "; " .. loop_condition .. "; " .. var_name .. " = LuaValue(get_double(" .. var_name .. ") + get_double(" .. step_expr_code .. "))) {\n" .. translate_node_to_cpp(body_node, for_header, false, current_module_object_name) .. "}"
        elseif node.type == "for_generic_statement" then
            local var_list_node = node.ordered_children[1]
            local expr_list_node = node.ordered_children[2]
            local body_node = node.ordered_children[3]

            local iterator_call_code = translate_node_to_cpp(expr_list_node, for_header, false, current_module_object_name)
            
            local loop_vars = {}
            for _, var_node in ipairs(var_list_node.ordered_children) do
                table.insert(loop_vars, var_node.identifier)
                declared_variables[var_node.identifier] = true
            end
            local loop_vars_declaration = ""
            for _, var_name in ipairs(loop_vars) do
                loop_vars_declaration = loop_vars_declaration .. "LuaValue " .. var_name .. "; "
            end

            -- Assuming the iterator_call_code returns a vector of {iterator_func, state, initial_value}
            -- This is a simplified approach and might need adjustment based on actual pairs/ipairs implementation
            local iter_func_var = "iter_func_" .. tostring(math.random(10000))
            local iter_state_var = "iter_state_" .. tostring(math.random(10000))
            local iter_value_var = "iter_value_" .. tostring(math.random(10000))
            local results_var = "iter_results_" .. tostring(math.random(10000))

            local cpp_code = ""
            cpp_code = cpp_code .. "std::vector<LuaValue> " .. results_var .. " = " .. iterator_call_code .. ";\n"
            cpp_code = cpp_code .. "auto " .. iter_func_var .. " = std::get<std::shared_ptr<LuaFunctionWrapper>>(" .. results_var .. "[0]);\n"
            cpp_code = cpp_code .. "LuaValue " .. iter_state_var .. " = " .. results_var .. "[1];\n"
            cpp_code = cpp_code .. "LuaValue " .. iter_value_var .. " = " .. results_var .. "[2];\n"
            
            cpp_code = cpp_code .. "while (true) {\n"
            cpp_code = cpp_code .. "    auto args_obj = std::make_shared<LuaObject>();\n"
            cpp_code = cpp_code .. "    args_obj->set(\"1\", " .. iter_state_var .. ");\n"
            cpp_code = cpp_code .. "    args_obj->set(\"2\", " .. iter_value_var .. ");\n"
            cpp_code = cpp_code .. "    std::vector<LuaValue> current_values = " .. iter_func_var .. "->func(args_obj);\n"
            
            cpp_code = cpp_code .. "    if (current_values.empty() || std::holds_alternative<std::monostate>(current_values[0])) {\n"
            cpp_code = cpp_code .. "        break;\n"
            cpp_code = cpp_code .. "    }\n"

            for i, var_name in ipairs(loop_vars) do
                cpp_code = cpp_code .. "    LuaValue " .. var_name .. " = current_values[" .. (i - 1) .. "];\n"
            end
            cpp_code = cpp_code .. "    " .. iter_value_var .. " = " .. loop_vars[1] .. ";\n" -- The first return value becomes the next 'var' for the iterator

            cpp_code = cpp_code .. translate_node_to_cpp(body_node, for_header, false, current_module_object_name) .. "\n"
            cpp_code = cpp_code .. "}\n"
            return cpp_code
        elseif node.type == "while_statement" then
            local condition = translate_node_to_cpp(node.ordered_children[1], for_header, false, current_module_object_name)
            local body = translate_node_to_cpp(node.ordered_children[2], for_header, false, current_module_object_name)
            -- FIX: Wrap condition in is_lua_truthy
            return "while (is_lua_truthy(" .. condition .. ")) {\n" .. body .. "}"
        elseif node.type == "call_expression" then
            local func_node = node.ordered_children[1]
            local args_code_builder = "std::make_shared<LuaObject>()"
            local args_init_code = ""
            for i, arg_node in ipairs(node.ordered_children) do
                if i > 1 then
                    args_init_code = args_init_code .. "temp_args->set_item(\"" .. tostring(i-1) .. "\", " .. translate_node_to_cpp(arg_node, for_header, false, current_module_object_name) .. ");\n"
                end
            end
            local args_code = "( [&]() { auto temp_args = " .. args_code_builder .. ";\n" .. args_init_code .. "return temp_args; } )()"

            if func_node.type == "member_expression" and func_node.ordered_children[1].identifier == "string" and func_node.ordered_children[2].identifier == "match" then
                local base_string_node = node.ordered_children[2]
                local pattern_node = node.ordered_children[3]
                local base_string_code = translate_node_to_cpp(base_string_node, for_header, false, current_module_object_name)
                local pattern_code = translate_node_to_cpp(pattern_node, for_header, false, current_module_object_name)
                return "{LuaValue(std::regex_search(" .. base_string_code .. ", std::regex(" .. pattern_code .. ")))}"
            elseif func_node.type == "member_expression" and func_node.ordered_children[1].identifier == "string" and func_node.ordered_children[2].identifier == "find" then
                local base_string_node = node.ordered_children[2]
                local pattern_node = node.ordered_children[3]
                local base_string_code = translate_node_to_cpp(base_string_node, for_header, false, current_module_object_name)
                local pattern_code = translate_node_to_cpp(pattern_node, for_header, false, current_module_object_name)
                return "{LuaValue(" .. base_string_code .. ".find(" .. pattern_code .. ") != std::string::npos ? static_cast<double>(" .. base_string_code .. ".find(" .. pattern_code .. ") + 1) : 0.0)}"
            elseif func_node.type == "member_expression" and func_node.ordered_children[1].identifier == "string" and func_node.ordered_children[2].identifier == "gsub" then
                local base_string_node = node.ordered_children[2]
                local pattern_node = node.ordered_children[3]
                local replacement_node = node.ordered_children[4]
                local base_string_code = translate_node_to_cpp(base_string_node, for_header, false, current_module_object_name)
                local pattern_code = translate_node_to_cpp(pattern_node, for_header, false, current_module_object_name)
                local replacement_code = translate_node_to_cpp(replacement_node, for_header, false, current_module_object_name)
                return "{LuaValue(std::regex_replace(" .. base_string_code .. ", std::regex(" .. pattern_code .. "), " .. replacement_code .. "))}"
            elseif func_node.type == "identifier" and func_node.identifier == "print" then
                local cpp_code = ""
                for i, arg_node in ipairs(node.ordered_children) do
                    if i > 1 then
                        local translated_arg = get_single_lua_value_cpp_code(arg_node, for_header, false, current_module_object_name)
                        cpp_code = cpp_code .. "print_value(" .. translated_arg .. ");"
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
                local translated_table = translate_node_to_cpp(table_node, for_header, false, current_module_object_name)
                local translated_metatable = translate_node_to_cpp(metatable_node, for_header, false, current_module_object_name)
                return "get_object(" .. translated_table .. ")->set_metatable(get_object(" .. translated_metatable .. "));"

            else
                local translated_func_access = ""
                if func_node.type == "identifier" and not declared_variables[func_node.identifier] then
                    translated_func_access = "_G->get(\"" .. func_node.identifier .. "\")"
                else
                    translated_func_access = translate_node_to_cpp(func_node, for_header, false, current_module_object_name)
                end
                return "std::get<std::shared_ptr<LuaFunctionWrapper>>(" .. translated_func_access .. ")->func(" .. args_code .. ")"
            end
        elseif node.type == "member_expression" then
            local base_node = node.ordered_children[1]
            local member_node = node.ordered_children[2]
            local base_code = translate_node_to_cpp(base_node, for_header, false, current_module_object_name)
            if base_node.type == "identifier" and (base_node.identifier == "math" or base_node.identifier == "string" or base_node.identifier == "table" or base_node.identifier == "os" or base_node.identifier == "io" or base_node.identifier == "package") then
                return "get_object(_G->get_item(\"" .. base_node.identifier .. "\"))->get_item(\"" .. member_node.identifier .. "\")"
            else
                return "get_object(" .. base_code .. ")->get_item(\"" .. member_node.identifier .. "\")"
            end
        elseif node.type == "string" then
            local s = node.value
            s = s:gsub('\\', '\\\\') -- Escape backslashes first
            s = s:gsub('"', '\\"')   -- Escape double quotes
            s = s:gsub('\n', '\\n')  -- Escape newlines
            s = s:gsub('\t', '\\t')  -- Escape tabs
            s = s:gsub('\r', '\\r')  -- Escape carriage returns
            return "\"" .. s .. "\""
        elseif node.type == "number" then
            local num_str = tostring(node.value)
            -- Check if the number is an integer (no decimal point)
            if not string.find(num_str, "%.") then
                return "LuaValue(static_cast<long long>(" .. num_str .. "))"
            else
                return "LuaValue(" .. num_str .. ")"
            end
        elseif node.type == "integer" then
            local num_str = tostring(node.value)
            return "LuaValue(static_cast<long long>(" .. num_str .. "))"
        elseif node.type == "identifier" then
            if node.identifier == "_VERSION" then
                return "get_object(_G)->get_item(\"_VERSION\")"
            elseif node.identifier == "nil" then
                return "std::monostate{}"
            elseif node.identifier == "math" or node.identifier == "string" or node.identifier == "table" or node.identifier == "os" or node.identifier == "io" or node.identifier == "package" or node.identifier == "utf8" or node.identifier == "debug" then
                return "get_object(_G->get_item(\"" .. node.identifier .. "\"))"
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
                local key_child = field_node.ordered_children[1]
                local value_child = field_node.ordered_children[2] -- This might be nil for list-style

                if value_child then -- This means it's either `id = expr` or `[expr] = expr`
                    if key_child.type == "identifier" then -- `id = expr`
                        key_part = "LuaValue(\"" .. key_child.identifier .. "\")"
                    else -- `[expr] = expr`
                        key_part = translate_node_to_cpp(key_child, for_header, false, current_module_object_name)
                    end
                    value_part = translate_node_to_cpp(value_child, for_header, false, current_module_object_name)
                else -- List-style field (value only)
                    value_child = key_child -- The single child is the value
                    key_part = "LuaValue(" .. tostring(list_index) .. "LL)" -- Use long long for integer keys
                    value_part = translate_node_to_cpp(value_child, for_header, false, current_module_object_name)
                    list_index = list_index + 1
                end
                if key_part and value_part then
                    construction_code = construction_code .. temp_table_var .. "->set_item(" .. key_part .. ", " .. value_part .. ");\n"
                end
            end
        return "( [&]() { " .. construction_code .. " return " .. temp_table_var .. "; } )()"
    elseif node.type == "method_call_expression" then
        local base_node = node.ordered_children[1]
        local method_node = node.ordered_children[2]
        local cpp_code = translate_node_to_cpp(base_node, for_header, false, current_module_object_name) .. "->" .. method_node.identifier .. "("
        for i, arg_node in ipairs(node.ordered_children) do
            if i > 2 then
                cpp_code = cpp_code .. translate_node_to_cpp(arg_node, for_header, false, current_module_object_name)
                if i < #node.ordered_children then
                    cpp_code = cpp_code .. ", "
                end
            end
        end
        return cpp_code .. ")"
    elseif node.type == "expression_statement" then
        local expr_node = node.ordered_children[1]
        if expr_node then
            local translated_expr = translate_node_to_cpp(expr_node, for_header, false, current_module_object_name)
            return translated_expr .. ";"
        end
        return ""
    elseif node.type == "table_index_expression" then
        local base_node = node.ordered_children[1]
        local index_node = node.ordered_children[2]
        local translated_base = translate_node_to_cpp(base_node, for_header, false, current_module_object_name)
        local translated_index = translate_node_to_cpp(index_node, for_header, false, current_module_object_name)
        return "get_object(" .. translated_base .. ")->get_item(" .. translated_index .. ")"
    elseif node.type == "function_declaration" or node.type == "method_declaration" then
        local func_name = node.identifier
        if node.method_name then
            func_name = func_name .. "_" .. node.method_name
        end
        local params_node = node.ordered_children[1]
        local body_node = node.ordered_children[2]
        local return_type = "std::vector<LuaValue>"

        local params_extraction = ""
        for i, param_node in ipairs(params_node.ordered_children) do
            params_extraction = params_extraction .. "    LuaValue " .. param_node.identifier .. " = args->get_item(\"" .. tostring(i) .. "\");\n"
        end

        local lambda_body = translate_node_to_cpp(body_node, for_header, false, current_module_object_name)
        local has_explicit_return = false
        for _, child_statement in ipairs(body_node.ordered_children) do
            if child_statement.type == "return_statement" then
                has_explicit_return = true
                break
            end
        end
        if not has_explicit_return then
            lambda_body = lambda_body .. "return {std::monostate{}};\n"
        end

        local lambda_code = "std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> " .. return_type .. " {\n" .. params_extraction .. lambda_body .. "})"

        if is_lambda then
            return lambda_code
        end

        if node.method_name ~= nil then
            return "get_object(LuaValue(" .. node.identifier .. "))->set(\"" .. node.method_name .. "\", " .. lambda_code .. ");"
        elseif node.identifier ~= nil then
            return "_G->set(\"" .. node.identifier .. "\", " .. lambda_code .. ");"
        else
            return lambda_code
        end
        elseif node.type == "return_statement" then
            local expr_list_node = node.ordered_children[1]
            if expr_list_node and #expr_list_node.ordered_children > 0 then
                local return_values = {}
                for _, expr_node in ipairs(expr_list_node.ordered_children) do
                    table.insert(return_values, translate_node_to_cpp(expr_node, for_header, false, current_module_object_name))
                end
                return "return {" .. table.concat(return_values, ", ") .. "};"
            else
                return "return {std::monostate{}};"
            end
        elseif node.type == "identifier" and node.identifier == "__index" then
            return "\"__index\""
        elseif node.type == "unary_expression" then
            local operator = node.value
            local operand_node = node.ordered_children[1]
            local translated_operand = translate_node_to_cpp(operand_node, for_header, false, current_module_object_name)
            if operator == "-" then
                return "LuaValue(-get_double(" .. translated_operand .. "))"
            elseif operator == "not" then
                -- FIX: Implement Lua 'not' logic
                return "LuaValue(!is_lua_truthy(" .. translated_operand .. "))"
            else
                return operator .. translated_operand
            end
        elseif node.type == "expression_list" then
            local return_values = {}
            for _, expr_node in ipairs(node.ordered_children) do
                table.insert(return_values, translate_node_to_cpp(expr_node, for_header, false, current_module_object_name))
            end
            return "{" .. table.concat(return_values, ", ") .. "}"
        elseif node.type == "block" then
            local block_cpp_code = ""
            for _, child in ipairs(node.ordered_children) do
                block_cpp_code = block_cpp_code .. "    " .. translate_node_to_cpp(child, for_header, false, current_module_object_name) .. "\n"
            end
            return block_cpp_code
        else
            print("WARNING: Unhandled node type: " .. node.type)
            return "/* UNHANDLED_NODE_TYPE: " .. node.type .. " */"
        end
    end
    
    local generated_code = translate_node_to_cpp(ast_root, for_header, false, current_module_object_name)
    
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
    header = header .. "#include \"utf8.hpp\"\n"
    header = header .. "#include \"init.hpp\"\n"
    
    if is_main_script then -- CHECK: Use is_main_script flag
        local main_function_start = "int main() {\n" ..
                                    "init_G();\n"
        local main_function_end = "\n    return 0;\n}"
        return header .. main_function_start .. generated_code .. main_function_end
    else -- Module generation
        local namespace_start = "namespace " .. file_name .. " {\n"
        local namespace_end = "\n} // namespace " .. file_name .. "\n"
        if for_header then -- Generate .hpp content
            local hpp_header = "#pragma once\n#include \"lua_object.hpp\"\n\n"
            local hpp_load_function_declaration = "std::vector<LuaValue> load();\n"
            -- Add extern declarations for global variables
            local global_var_extern_declarations = ""
            for var_name, _ in pairs(module_global_vars) do
                global_var_extern_declarations = global_var_extern_declarations .. "extern LuaValue " .. var_name .. ";\n"
            end
            return hpp_header .. global_var_extern_declarations .. namespace_start .. hpp_load_function_declaration .. namespace_end
        else -- Generate .cpp content
            local cpp_header = "#include \"" .. file_name .. ".hpp\"\n"
            -- Add definitions for global variables
            local global_var_definitions = ""
            for var_name, _ in pairs(module_global_vars) do
                global_var_definitions = global_var_definitions .. "LuaValue " .. var_name .. ";\n"
            end
            local module_body_code = ""
            local module_identifier = ""
            local explicit_return_found = false
            local explicit_return_value = "std::make_shared<LuaObject>()" -- Default to empty table

            -- Find the module's local declaration and process statements
            for _, child in ipairs(ast_root.ordered_children) do
                if child.type == "local_declaration" and #child.ordered_children >= 2 and child.ordered_children[1].type == "variable" and child.ordered_children[2].type == "table_constructor" then
                    module_identifier = child.ordered_children[1].identifier
                    module_body_code = module_body_code .. "std::shared_ptr<LuaObject> " .. module_identifier .. " = std::make_shared<LuaObject>();\n"
                elseif child.type == "return_statement" then
                    explicit_return_found = true
                    local return_expr_node = child.ordered_children[1]
                    if return_expr_node then
                        if return_expr_node.type == "expression_list" then
                            explicit_return_value = translate_node_to_cpp(return_expr_node, false, false, current_module_object_name)
                        else
                            explicit_return_value = translate_node_to_cpp(return_expr_node, false, false, current_module_object_name)
                        end
                    else
                        explicit_return_value = "std::monostate{}" -- Lua's implicit nil
                    end                    -- Do not append to module_body_code, as it's an explicit return for the module
                else
                    module_body_code = module_body_code .. translate_node_to_cpp(child, false, false, current_module_object_name) .. "\n"
                end
            end

            local load_function_body = ""
            load_function_body = load_function_body .. module_body_code

            if explicit_return_found then
                load_function_body = load_function_body .. "return {" .. explicit_return_value .. "};\n"
            elseif module_identifier ~= "" then
                load_function_body = load_function_body .. "return {" .. module_identifier .. "};\n"
            else
                load_function_body = load_function_body .. "return {std::make_shared<LuaObject>()};\n"
            end

            local load_function_definition = "std::vector<LuaValue> load() {\n" .. load_function_body .. "}\n"
            return header .. cpp_header .. global_var_definitions .. namespace_start .. load_function_definition .. namespace_end
        end
    end
end

return CppTranslator