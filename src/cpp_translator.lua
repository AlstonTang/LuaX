-- C++ Translator Module
local CppTranslator = {}
CppTranslator.__index = CppTranslator

-- C++ reserved keywords that need to be sanitized
local cpp_keywords = {
	["operator"] = true, ["class"] = true, ["private"] = true, ["public"] = true,
	["protected"] = true, ["namespace"] = true, ["template"] = true, ["typename"] = true,
	["static"] = true, ["const"] = true, ["volatile"] = true, ["mutable"] = true,
	["auto"] = true, ["register"] = true, ["extern"] = true, ["inline"] = true,
	["virtual"] = true, ["explicit"] = true, ["friend"] = true, ["typedef"] = true,
	["union"] = true, ["enum"] = true, ["struct"] = true, ["sizeof"] = true,
	["new"] = true, ["delete"] = true, ["this"] = true,
	-- Note: true/false handled separately in parser as boolean type
	["nullptr"] = true, ["void"] = true,
	["int"] = true, ["char"] = true, ["short"] = true, ["long"] = true,
	["float"] = true, ["double"] = true, ["signed"] = true, ["unsigned"] = true,
	["switch"] = true, ["case"] = true, ["default"] = true,
	-- Note: break/continue are Lua keywords too, handled in parser
	["using"] = true, ["try"] = true, ["catch"] = true,
	["throw"] = true, ["const_cast"] = true, ["static_cast"] = true,
	["dynamic_cast"] = true, ["reinterpret_cast"] = true
}

local function sanitize_cpp_identifier(name)
	if cpp_keywords[name] then
		return "lua_" .. name
	end
	return name
end

function CppTranslator:new()
	local instance = setmetatable({}, CppTranslator)
	instance.unique_id_counter = 0
	return instance
end

function CppTranslator:get_unique_id()
	self.unique_id_counter = self.unique_id_counter + 1
	return tostring(self.unique_id_counter)
end

function CppTranslator:translate_recursive(ast_root, file_name, for_header, current_module_object_name, is_main_script)
	local declared_variables = {}
	local required_modules = {}
	local module_global_vars = {}
	local current_function_fixed_params_count = 0
	local function translate_node_to_cpp(node, for_header, is_lambda, current_module_object_name, depth)
		depth = depth or 0
		if depth > 50 then
			print("ERROR: Recursion limit exceeded in translate_node_to_cpp. Node type: " .. (node and node.type or "nil"))
			os.exit(1)
		end
		if not node then
			return "" 
		end
		if not (node.type) then
			print("ERROR: Node has no type field:", node)
			return "/* ERROR: Node with no type */"
		end

		-- Helper function to get the C++ code for a single LuaValue
		-- This ensures that if the node is a function call (returning vector), we extract the first result
		local function get_single_lua_value_cpp_code(node_to_translate, for_header_arg, is_lambda_arg, current_module_object_name_arg)
			if not node_to_translate then return "std::monostate{}" end

			if node_to_translate.type == "call_expression" or node_to_translate.type == "method_call_expression" then
				local translated_code = translate_node_to_cpp(node_to_translate, for_header_arg, is_lambda_arg, current_module_object_name_arg, depth + 1)
				-- Wrap the call in a lambda to extract the first element
				return "( [=]() mutable -> LuaValue { auto results = " .. translated_code .. "; return results.empty() ? LuaValue(std::monostate{}) : results[0]; } )()"
			else
				-- For non-calls, translate directly.
				return translate_node_to_cpp(node_to_translate, for_header_arg, is_lambda_arg, current_module_object_name_arg, depth + 1)
			end
		end

		if node.type == "Root" then
			local cpp_code = ""
				for i, child in ipairs(node.ordered_children) do
					cpp_code = cpp_code .. translate_node_to_cpp(child, for_header, false, current_module_object_name, depth + 1) .. "\n"
				end
			return cpp_code
		elseif node.type == "block" then
			local block_code = "{\n"
			local old_declared_variables = {}
			for k,v in pairs(declared_variables) do old_declared_variables[k] = v end

			for _, child in ipairs(node.ordered_children) do
				block_code = block_code .. translate_node_to_cpp(child, for_header, false, current_module_object_name, depth + 1)
			end
			
			declared_variables = old_declared_variables
			block_code = block_code .. "}\n"
			return block_code
		elseif node.type == "identifier" then
			local decl_info = declared_variables[node.identifier]
			if decl_info then
				if type(decl_info) == "table" and decl_info.is_ptr then
					return "(*" .. decl_info.ptr_name .. ")"
				end
				return sanitize_cpp_identifier(node.identifier)
			end

			if node.identifier == "_VERSION" then
				return "get_object(_G)->get_item(\"_VERSION\")"
			elseif node.identifier == "nil" then
				return "std::monostate{}"
			elseif node.identifier == "math" or node.identifier == "string" or node.identifier == "table" or node.identifier == "os" or node.identifier == "io" or node.identifier == "package" or node.identifier == "utf8" or node.identifier == "debug" or node.identifier == "arg" or node.identifier == "coroutine" then
				return "get_object(_G->get_item(\"" .. node.identifier .. "\"))"
			elseif node.identifier == "type" then
				 return "_G->get_item(\"type\")"
			else
				return sanitize_cpp_identifier(node.identifier)
			end
		
		elseif node.type == "function_declaration" or node.type == "method_declaration" then
			local func_name = node.identifier
			if node.method_name then
				func_name = func_name .. "_" .. node.method_name
			end

			local ptr_name = nil
			local old_decl_info = nil

			local old_declared_variables = {}
			for k,v in pairs(declared_variables) do old_declared_variables[k] = v end
			
			if node.is_local and node.identifier then
				local var_name = node.identifier
				ptr_name = var_name .. "_ptr_" .. self:get_unique_id()
				
				local sanitized_var_name = sanitize_cpp_identifier(node.identifier)
				
				old_decl_info = declared_variables[sanitized_var_name]
				declared_variables[sanitized_var_name] = { is_ptr = true, ptr_name = ptr_name }
			end
			
			local params_node = node.ordered_children[1]
			local body_node = node.ordered_children[2]
			local return_type = "std::vector<LuaValue>"

			local prev_param_count = current_function_fixed_params_count
			current_function_fixed_params_count = 0

			local params_extraction = ""
			local param_index_offset = 0
			
			local params_scope = {}
			if node.type == "method_declaration" or node.is_method then
				params_extraction = params_extraction .. "    LuaValue self = (args.size() > 0 ? args[0] : LuaValue(std::monostate{}));\n"
				table.insert(params_scope, {name = "self", old_val = declared_variables["self"]})
				declared_variables["self"] = true
				param_index_offset = 1
			end
			
			current_function_fixed_params_count = param_index_offset

			for i, param_node in ipairs(params_node.ordered_children) do
				if param_node.type == "identifier" then
					current_function_fixed_params_count = current_function_fixed_params_count + 1
					local param_name = param_node.identifier
					local vector_idx = i + param_index_offset - 1
					params_extraction = params_extraction .. "    LuaValue " .. param_name .. " = (args.size() > " .. vector_idx .. " ? args[" .. vector_idx .. "] : LuaValue(std::monostate{}));\n"
					table.insert(params_scope, {name = param_name, old_val = declared_variables[param_name]})
					declared_variables[param_name] = true
				end
			end
			local lambda_body = translate_node_to_cpp(body_node, for_header, false, current_module_object_name, depth + 1)
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

			declared_variables = old_declared_variables
			
			if node.is_local and node.identifier then
				local var_name = sanitize_cpp_identifier(node.identifier)
				declared_variables[var_name] = old_decl_info 
			end

			for _, scope_info in ipairs(params_scope) do
				declared_variables[scope_info.name] = scope_info.old_val
			end

			local lambda_code = "std::make_shared<LuaFunctionWrapper>([=](std::vector<LuaValue> args) mutable -> " .. return_type .. " {\n" .. params_extraction .. lambda_body .. "})"

			current_function_fixed_params_count = prev_param_count

			if is_lambda then
				return lambda_code
			end

			if node.method_name ~= nil then
				return "get_object(LuaValue(" .. node.identifier .. "))->set(\"" .. node.method_name .. "\", " .. lambda_code .. ");"
			elseif node.identifier ~= nil then
				if node.is_local then
					 local var_name = sanitize_cpp_identifier(node.identifier)
					 declared_variables[var_name] = true 
					 return "auto " .. ptr_name .. " = std::make_shared<LuaValue>();\n" ..
							"*" .. ptr_name .. " = " .. lambda_code .. ";\n" ..
							"LuaValue " .. var_name .. " = *" .. ptr_name .. ";"
				else
					return "_G->set(\"" .. node.identifier .. "\", " .. lambda_code .. ");"
				end
			else
				return lambda_code
			end
		
		elseif node.type == "local_declaration" then
			local var_list_node = node.ordered_children[1]
			local expr_list_node = node.ordered_children[2]
			local cpp_code = ""

			local num_vars = #var_list_node.ordered_children
			local num_exprs = expr_list_node and #expr_list_node.ordered_children or 0

			-- Optimization: Single variable, single function call
			if num_vars == 1 and num_exprs == 1 then
				local expr_node = expr_list_node.ordered_children[1]
				if expr_node.type == "call_expression" or expr_node.type == "method_call_expression" then
					local var_node = var_list_node.ordered_children[1]
					local var_name = sanitize_cpp_identifier(var_node.identifier)
					local call_code = translate_node_to_cpp(expr_node, for_header, false, current_module_object_name, depth + 1)
					
					cpp_code = cpp_code .. "LuaValue " .. var_name .. " = get_return_value(" .. call_code .. ", 0);\n"
					declared_variables[var_name] = true
					return cpp_code
				end
			end

			local function_call_results_var = "func_results_" .. self:get_unique_id()
			local has_function_call_expr = false
			local first_call_expr_node = nil

			for i = 1, num_exprs do
				local expr_node = expr_list_node.ordered_children[i]
				if expr_node.type == "call_expression" or expr_node.type == "method_call_expression" then
					has_function_call_expr = true
					first_call_expr_node = expr_node
					break
				end
			end

			if has_function_call_expr and first_call_expr_node then
				cpp_code = cpp_code .. "std::vector<LuaValue> " .. function_call_results_var .. " = " .. translate_node_to_cpp(first_call_expr_node, for_header, false, current_module_object_name, depth + 1) .. ";\n"
			end

			for i = 1, num_vars do
				local var_node = var_list_node.ordered_children[i]
				local var_name = sanitize_cpp_identifier(var_node.identifier)
				local var_type = "LuaValue" 

				local initial_value_code = "std::monostate{}" 

				if i <= num_exprs then
					local expr_node = expr_list_node.ordered_children[i]
					if expr_node.type == "call_expression" or expr_node.type == "method_call_expression" then
						initial_value_code = "(" .. function_call_results_var .. ".size() > " .. (i - 1) .. " ? " .. function_call_results_var .. "[" .. (i - 1) .. "] : std::monostate{})"
					else
						initial_value_code = translate_node_to_cpp(expr_node, for_header, false, current_module_object_name, depth + 1)

					end
				elseif has_function_call_expr then
					initial_value_code = "(" .. function_call_results_var .. ".size() > " .. (i - 1) .. " ? " .. function_call_results_var .. "[" .. (i - 1) .. "] : std::monostate{})"
				end
				
				cpp_code = cpp_code .. var_type .. " " .. var_name .. " = " .. initial_value_code .. ";\n"
				declared_variables[var_name] = true
			end
			return cpp_code
		elseif node.type == "assignment" then
			local var_list_node = node.ordered_children[1]
			local expr_list_node = node.ordered_children[2]
			local cpp_code = ""

			local num_vars = #var_list_node.ordered_children
			local num_exprs = #expr_list_node.ordered_children

			-- Optimization: Single variable, single function call
			if num_vars == 1 and num_exprs == 1 then
				local expr_node = expr_list_node.ordered_children[1]
				if expr_node.type == "call_expression" or expr_node.type == "method_call_expression" then
					local var_node = var_list_node.ordered_children[1]
					local call_code = translate_node_to_cpp(expr_node, for_header, false, current_module_object_name, depth + 1)
					local value_code = "get_return_value(" .. call_code .. ", 0)"

					if var_node.type == "member_expression" then
						local base_node = var_node.ordered_children[1]
						local member_node = var_node.ordered_children[2]
						local translated_base = get_single_lua_value_cpp_code(base_node, for_header, false, current_module_object_name)
						local member_name = member_node.identifier
						cpp_code = cpp_code .. "get_object(" .. translated_base .. ")->set(\"" .. member_name .. "\", " .. value_code .. ");\n"
					elseif var_node.type == "table_index_expression" then
						local base_node = var_node.ordered_children[1]
						local index_node = var_node.ordered_children[2]
						local translated_base = get_single_lua_value_cpp_code(base_node, for_header, false, current_module_object_name)
						local translated_index = get_single_lua_value_cpp_code(index_node, for_header, false, current_module_object_name)
						cpp_code = cpp_code .. "get_object(" .. translated_base .. ")->set_item(" .. translated_index .. ", " .. value_code .. ");\n"
					else
						local var_code = translate_node_to_cpp(var_node, for_header, false, current_module_object_name, depth + 1)
						local declaration_prefix = ""
						if var_node.type == "identifier" and not declared_variables[var_node.identifier] then
							if is_main_script then 
								declaration_prefix = "LuaValue "
							else
								module_global_vars[var_node.identifier] = true
							end
							declared_variables[var_node.identifier] = true
						end
						cpp_code = cpp_code .. declaration_prefix .. var_code .. " = " .. value_code .. ";\n"
					end
					return cpp_code
				end
			end

			local function_call_results_var = "func_results_" .. tostring(os.time()) .. "_" .. self:get_unique_id()
			local has_function_call_expr = false
			local first_call_expr_node = nil

			for i = 1, num_exprs do
				local expr_node = expr_list_node.ordered_children[i]
				if expr_node.type == "call_expression" or expr_node.type == "method_call_expression" then
					has_function_call_expr = true
					first_call_expr_node = expr_node
					break
				end
			end

			if has_function_call_expr and first_call_expr_node then
				cpp_code = cpp_code .. "std::vector<LuaValue> " .. function_call_results_var .. " = " .. translate_node_to_cpp(first_call_expr_node, for_header, false, current_module_object_name, depth + 1) .. ";\n"
			end

			for i = 1, num_vars do
				local var_node = var_list_node.ordered_children[i]
				local value_code = "std::monostate{}" 

				if i <= num_exprs then
					local expr_node = expr_list_node.ordered_children[i]
					if expr_node.type == "call_expression" or expr_node.type == "method_call_expression" then
						value_code = "(" .. function_call_results_var .. ".size() > " .. (i - 1) .. " ? " .. function_call_results_var .. "[" .. (i - 1) .. "] : std::monostate{})"
					else
						value_code = translate_node_to_cpp(expr_node, for_header, false, current_module_object_name, depth + 1)
					end
				elseif has_function_call_expr then
					value_code = "(" .. function_call_results_var .. ".size() > " .. (i - 1) .. " ? " .. function_call_results_var .. "[" .. (i - 1) .. "] : std::monostate{})"
				end

				if var_node.type == "member_expression" then
					local base_node = var_node.ordered_children[1]
					local member_node = var_node.ordered_children[2]
					local translated_base = get_single_lua_value_cpp_code(base_node, for_header, false, current_module_object_name)
					local member_name = member_node.identifier
					cpp_code = cpp_code .. "get_object(" .. translated_base .. ")->set(\"" .. member_name .. "\", " .. value_code .. ");\n"
				elseif var_node.type == "table_index_expression" then
					local base_node = var_node.ordered_children[1]
					local index_node = var_node.ordered_children[2]
					local translated_base = get_single_lua_value_cpp_code(base_node, for_header, false, current_module_object_name)
					local translated_index = get_single_lua_value_cpp_code(index_node, for_header, false, current_module_object_name)
					cpp_code = cpp_code .. "get_object(" .. translated_base .. ")->set_item(" .. translated_index .. ", " .. value_code .. ");\n"
				else
					local var_code = translate_node_to_cpp(var_node, for_header, false, current_module_object_name, depth + 1)
					local declaration_prefix = ""
					if var_node.type == "identifier" and not declared_variables[var_node.identifier] then
						if is_main_script then 
							declaration_prefix = "LuaValue "
						else
							module_global_vars[var_node.identifier] = true
						end
						declared_variables[var_node.identifier] = true
					end
					cpp_code = cpp_code .. declaration_prefix .. var_code .. " = " .. value_code .. ";\n"
				end
			end
			return cpp_code
		elseif node.type == "binary_expression" then
			local operator = node.value
			local left = get_single_lua_value_cpp_code(node.ordered_children[1], for_header, false, current_module_object_name)
			local right = get_single_lua_value_cpp_code(node.ordered_children[2], for_header, false, current_module_object_name)
			
			if operator == "+" then
				return "(get_double(" .. left .. ") + get_double(" .. right .. "))"
			elseif operator == "-" then
				return "(get_double(" .. left .. ") - get_double(" .. right .. "))"
			elseif operator == "*" then
				return "(get_double(" .. left .. ") * get_double(" .. right .. "))"
			elseif operator == "/" then
				return "(get_double(" .. left .. ") / get_double(" .. right .. "))"
			elseif operator == "//" then
				return "LuaValue(static_cast<long long>(std::floor(get_double(" .. left .. ") / get_double(" .. right .. "))))"
			elseif operator == "%" then
				return "fmod(get_double(" .. left .. "), get_double(" .. right .. "))"
			elseif operator == "^" then
				return "pow(get_double(" .. left .. "), get_double(" .. right .. "))"
			elseif operator == "&" then
				return "LuaValue(static_cast<long long>(get_long_long(" .. left .. ") & get_long_long(" .. right .. ")))"
			elseif operator == "|" then
				return "LuaValue(static_cast<long long>(get_long_long(" .. left .. ") | get_long_long(" .. right .. ")))"
			elseif operator == "~" then
				return "LuaValue(static_cast<long long>(get_long_long(" .. left .. ") ^ get_long_long(" .. right .. ")))"
			elseif operator == "<<" then
				return "LuaValue(static_cast<long long>(get_long_long(" .. left .. ") << get_long_long(" .. right .. ")))"
			elseif operator == ">>" then
				return "LuaValue(static_cast<long long>(get_long_long(" .. left .. ") >> get_long_long(" .. right .. ")))"
			elseif operator == "==" then
				return "lua_equals(" .. left .. ", " .. right .. ")"
			elseif operator == "~=" then
				return "(!is_lua_truthy(lua_equals(" .. left .. ", " .. right .. ")))"
			elseif operator == "<" then
				return "lua_less_than(" .. left .. ", " .. right .. ")"
			elseif operator == ">" then
				return "lua_greater_than(" .. left .. ", " .. right .. ")"
			elseif operator == "<=" then
				return "lua_less_equals(" .. left .. ", " .. right .. ")"
			elseif operator == ">=" then
				return "lua_greater_equals(" .. left .. ", " .. right .. ")"
			elseif operator == "and" then
				local id = self:get_unique_id()
				return "( [=]() mutable -> LuaValue { const auto left_and_val_" .. id .. " = " .. left .. "; return is_lua_truthy(left_and_val_" .. id .. ") ? LuaValue(" .. right .. ") : LuaValue(left_and_val_" .. id .. "); } )()"
			elseif operator == "or" then
				local id = self:get_unique_id()
				return "( [=]() mutable -> LuaValue { const auto left_or_val_" .. id .. " = " .. left .. "; return is_lua_truthy(left_or_val_" .. id .. ") ? LuaValue(left_or_val_" .. id .. ") : LuaValue(" .. right .. "); } )()"
			elseif operator == ".." then
				return "lua_concat(" .. left .. ", " .. right .. ")"
			else
				return operator .. "(" .. left .. ", " .. right .. ")"
			end
		
		elseif node.type == "if_statement" then
			local cpp_code = ""
			local first_clause = true
			for i, clause in ipairs(node.ordered_children) do
				if clause.type == "if_clause" then
					local condition = get_single_lua_value_cpp_code(clause.ordered_children[1], for_header, false, current_module_object_name)
					local body = translate_node_to_cpp(clause.ordered_children[2], for_header, false, current_module_object_name, depth + 1)
					cpp_code = cpp_code .. "if (is_lua_truthy(" .. condition .. ")) {\n" .. body .. "}"
					first_clause = false
				elseif clause.type == "elseif_clause" then
					local condition = get_single_lua_value_cpp_code(clause.ordered_children[1], for_header, false, current_module_object_name)
					local body = translate_node_to_cpp(clause.ordered_children[2], for_header, false, current_module_object_name, depth + 1)
					cpp_code = cpp_code .. " else if (is_lua_truthy(" .. condition .. ")) {\n" .. body .. "}"
				elseif clause.type == "else_clause" then
					local body = translate_node_to_cpp(clause.ordered_children[1], for_header, false, current_module_object_name, depth + 1)
					cpp_code = cpp_code .. " else {\n" .. body .. "}"
				end
			end
			return cpp_code .. "\n"
		elseif node.type == "expression_statement" then
			return translate_node_to_cpp(node.ordered_children[1], for_header, false, current_module_object_name, depth + 1) .. ";\n"
		
		elseif node.type == "call_expression" then
			local func_node = node.ordered_children[1]
			
			local function is_table_unpack_call(arg_node)
				if arg_node.type == "call_expression" then
					local call_func = arg_node.ordered_children[1]
					if call_func.type == "member_expression" then
						local base = call_func.ordered_children[1]
						local member = call_func.ordered_children[2]
						if base.identifier == "table" and member.identifier == "unpack" then
							return true
						end
					end
				end
				return false
			end
			
			-- Check if any argument is a table.unpack call which requires dynamic vector construction
			local has_complex_args = false
			for i = 2, #node.ordered_children do
				if is_table_unpack_call(node.ordered_children[i]) then
					has_complex_args = true
					break
				end
			end

			local args_code
			
			if has_complex_args then
				local temp_args_var = "temp_args_" .. self:get_unique_id()
				local args_init_code = ""
				
				for i = 2, #node.ordered_children do
					local arg_node = node.ordered_children[i]
					if is_table_unpack_call(arg_node) then
						local unpack_table_arg = arg_node.ordered_children[2]
						local unpack_vec_var = "unpack_vec_" .. self:get_unique_id()
						local translated_table = translate_node_to_cpp(unpack_table_arg, for_header, false, current_module_object_name, depth + 1)
						
						args_init_code = args_init_code .. "auto " .. unpack_vec_var .. " = call_lua_value(get_object(_G->get_item(\"table\"))->get_item(\"unpack\"), ( [=]() mutable { std::vector<LuaValue> temp_unpack_args; temp_unpack_args.push_back(" .. translated_table .. "); return temp_unpack_args; } )()); "
						args_init_code = args_init_code .. temp_args_var .. ".insert(" .. temp_args_var .. ".end(), " .. unpack_vec_var .. ".begin(), " .. unpack_vec_var .. ".end()); "
					else
						-- IMPORTANT: Use get_single_lua_value_cpp_code to ensure we get a LuaValue, not a vector expression
						local arg_val_code = get_single_lua_value_cpp_code(arg_node, for_header, false, current_module_object_name)
						args_init_code = args_init_code .. temp_args_var .. ".push_back(" .. arg_val_code .. "); "
					end
				end
				args_code = "( [=]() mutable { std::vector<LuaValue> " .. temp_args_var .. "; " .. args_init_code .. "return " .. temp_args_var .. "; } )()"
			else
				-- Clean initialization using initializer list
				local arg_list_str = ""
				for i = 2, #node.ordered_children do
					-- IMPORTANT: Use get_single_lua_value_cpp_code to ensure we get a LuaValue
					local arg_val_code = get_single_lua_value_cpp_code(node.ordered_children[i], for_header, false, current_module_object_name)
					arg_list_str = arg_list_str .. arg_val_code
					if i < #node.ordered_children then
						arg_list_str = arg_list_str .. ", "
					end
				end
				args_code = "std::vector<LuaValue>{" .. arg_list_str .. "}"
			end

			if func_node.type == "member_expression" and func_node.ordered_children[1].identifier == "string" and func_node.ordered_children[2].identifier == "match" then
				local base_string_node = node.ordered_children[2]
				local pattern_node = node.ordered_children[3]
				local base_string_code = get_single_lua_value_cpp_code(base_string_node, for_header, false, current_module_object_name)
				local pattern_code = get_single_lua_value_cpp_code(pattern_node, for_header, false, current_module_object_name)
				return "lua_string_match(" .. base_string_code .. ", " .. pattern_code .. ")"
			elseif func_node.type == "member_expression" and func_node.ordered_children[1].identifier == "string" and func_node.ordered_children[2].identifier == "find" then
				local base_string_node = node.ordered_children[2]
				local pattern_node = node.ordered_children[3]
				local base_string_code = get_single_lua_value_cpp_code(base_string_node, for_header, false, current_module_object_name)
				local pattern_code = get_single_lua_value_cpp_code(pattern_node, for_header, false, current_module_object_name)
				return "lua_string_find(" .. base_string_code .. ", " .. pattern_code .. ")"
			elseif func_node.type == "member_expression" and func_node.ordered_children[1].identifier == "string" and func_node.ordered_children[2].identifier == "gsub" then
				local base_string_node = node.ordered_children[2]
				local pattern_node = node.ordered_children[3]
				local replacement_node = node.ordered_children[4]
				local base_string_code = get_single_lua_value_cpp_code(base_string_node, for_header, false, current_module_object_name)
				local pattern_code = get_single_lua_value_cpp_code(pattern_node, for_header, false, current_module_object_name)
				local replacement_code = get_single_lua_value_cpp_code(replacement_node, for_header, false, current_module_object_name)
				return "lua_string_gsub(" .. base_string_code .. ", " .. pattern_code .. ", " .. replacement_code .. ")"
			elseif func_node.type == "identifier" and func_node.identifier == "print" then
				local cpp_code = ""
				for i, arg_node in ipairs(node.ordered_children) do
					if i > 1 then
						local translated_arg = get_single_lua_value_cpp_code(arg_node, for_header, false, current_module_object_name)
						cpp_code = cpp_code .. "print_value(" .. translated_arg .. ");"
						if i < #node.ordered_children then
							cpp_code = cpp_code .. "std::cout << \"\\t\";" 
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
					local sanitized_module_name = module_name:gsub("%.", "_")
					return sanitized_module_name .. "::load()"
				end
				return "/* require call with non-string argument */"
			elseif func_node.type == "identifier" and func_node.identifier == "setmetatable" then
				local table_node = node.ordered_children[2]
				local metatable_node = node.ordered_children[3]
				local translated_table = translate_node_to_cpp(table_node, for_header, false, current_module_object_name, depth + 1)
				local translated_metatable = translate_node_to_cpp(metatable_node, for_header, false, current_module_object_name, depth + 1)
				return "( [=]() mutable -> std::vector<LuaValue> { auto tbl = " .. translated_table .. "; get_object(tbl)->set_metatable(get_object(" .. translated_metatable .. ")); return {tbl}; } )()"
			else
				local translated_func_access
				
				if func_node.type == "identifier" then
					local decl_info = declared_variables[func_node.identifier]
					if decl_info then
						if type(decl_info) == "table" and decl_info.is_ptr then
							translated_func_access = "(*" .. decl_info.ptr_name .. ")"
						else
							translated_func_access = sanitize_cpp_identifier(func_node.identifier)
						end
					else
						translated_func_access = "_G->get(\"" .. func_node.identifier .. "\")"
					end
				else
					translated_func_access = translate_node_to_cpp(func_node, for_header, false, current_module_object_name, depth + 1)
				end
				return "call_lua_value(" .. translated_func_access .. ", " .. args_code .. ")"
			end
		
		elseif node.type == "varargs" then
			local start_index = current_function_fixed_params_count
			return "( [=]() mutable -> std::vector<LuaValue> { if(args.size() > " .. start_index .. ") return std::vector<LuaValue>(args.begin() + " .. start_index .. ", args.end()); return {}; } )()"
		
		elseif node.type == "table_constructor" then
			local cpp_code = "std::make_shared<LuaObject>()"
			local fields = node:get_all_children_of_type("table_field")
			local list_index = 1
			local temp_table_var = "temp_table_" .. self:get_unique_id()
			local construction_code = "auto " .. temp_table_var .. " = " .. cpp_code .. ";\n"
			for i, field_node in ipairs(fields) do
				local key_part
				local value_part
				local key_child = field_node.ordered_children[1]
				local value_child = field_node.ordered_children[2] 
				if value_child then 
					if key_child.type == "identifier" then 
						key_part = "LuaValue(\"" .. key_child.identifier .. "\")"
					else 
						key_part = translate_node_to_cpp(key_child, for_header, false, current_module_object_name, depth + 1)
					end
					value_part = translate_node_to_cpp(value_child, for_header, false, current_module_object_name, depth + 1)
					construction_code = construction_code .. temp_table_var .. "->set_item(" .. key_part .. ", " .. value_part .. ");\n"
				else 
					value_child = key_child 
					if value_child.type == "varargs" then
						local varargs_vec = "varargs_vec_" .. self:get_unique_id()
						local varargs_code = translate_node_to_cpp(value_child, for_header, false, current_module_object_name, depth + 1)
						construction_code = construction_code .. "auto " .. varargs_vec .. " = " .. varargs_code .. ";\n"
						construction_code = construction_code .. "for (size_t i = 0; i < " .. varargs_vec .. ".size(); ++i) {\n"
						construction_code = construction_code .. "  " .. temp_table_var .. "->set_item(LuaValue(static_cast<long long>(" .. list_index .. " + i)), " .. varargs_vec .. "[i]);\n"
						construction_code = construction_code .. "}\n"
					else
						key_part = "LuaValue(" .. tostring(list_index) .. "LL)" 
						value_part = translate_node_to_cpp(value_child, for_header, false, current_module_object_name, depth + 1)
						construction_code = construction_code .. temp_table_var .. "->set_item(" .. key_part .. ", " .. value_part .. ");\n"
						list_index = list_index + 1
					end
				end
			end
			return "( [=]() mutable { " .. construction_code .. " return " .. temp_table_var .. "; } )()"
		elseif node.type == "method_call_expression" then
			local base_node = node.ordered_children[1]
			local method_node = node.ordered_children[2]
			local method_name = method_node.identifier
			local translated_base = get_single_lua_value_cpp_code(base_node, for_header, false, current_module_object_name)

			local function is_table_unpack_call(arg_node)
				if arg_node.type == "call_expression" then
					local call_func = arg_node.ordered_children[1]
					if call_func.type == "member_expression" then
						local base = call_func.ordered_children[1]
						local member = call_func.ordered_children[2]
						if base.identifier == "table" and member.identifier == "unpack" then
							return true
						end
					end
				end
				return false
			end

			if method_name == "match" then
				local pattern_code = get_single_lua_value_cpp_code(node.ordered_children[3], for_header, false, current_module_object_name)
				return "lua_string_match(" .. translated_base .. ", " .. pattern_code .. ")"
			elseif method_name == "find" then
				local pattern_code = get_single_lua_value_cpp_code(node.ordered_children[3], for_header, false, current_module_object_name)
				return "lua_string_find(" .. translated_base .. ", " .. pattern_code .. ")"
			elseif method_name == "gsub" then
				local pattern_code = get_single_lua_value_cpp_code(node.ordered_children[3], for_header, false, current_module_object_name)
				local replacement_code = get_single_lua_value_cpp_code(node.ordered_children[4], for_header, false, current_module_object_name)
				return "lua_string_gsub(" .. translated_base .. ", " .. pattern_code .. ", " .. replacement_code .. ")"
			else
				local func_access = "lua_get_member(" .. translated_base .. ", LuaValue(\"" .. method_name .. "\"))"
				
				local has_complex_args = false
				for i = 3, #node.ordered_children do
					if is_table_unpack_call(node.ordered_children[i]) then
						has_complex_args = true
						break
					end
				end

				local args_code
				
				if has_complex_args then
					local temp_args_var = "temp_args_" .. self:get_unique_id()
					local args_init_code = ""
					
					-- Self argument
					args_init_code = args_init_code .. temp_args_var .. ".push_back(" .. translated_base .. "); "

					for i = 3, #node.ordered_children do
						local arg_node = node.ordered_children[i]
						if is_table_unpack_call(arg_node) then
							local unpack_table_arg = arg_node.ordered_children[2]
							local unpack_vec_var = "unpack_vec_" .. self:get_unique_id()
							local translated_table = translate_node_to_cpp(unpack_table_arg, for_header, false, current_module_object_name, depth + 1)
							
							args_init_code = args_init_code .. "auto " .. unpack_vec_var .. " = call_lua_value(get_object(_G->get_item(\"table\"))->get_item(\"unpack\"), ( [=]() mutable { std::vector<LuaValue> temp_unpack_args; temp_unpack_args.push_back(" .. translated_table .. "); return temp_unpack_args; } )()); "
							args_init_code = args_init_code .. temp_args_var .. ".insert(" .. temp_args_var .. ".end(), " .. unpack_vec_var .. ".begin(), " .. unpack_vec_var .. ".end()); "
						else
							local arg_val_code = get_single_lua_value_cpp_code(arg_node, for_header, false, current_module_object_name)
							args_init_code = args_init_code .. temp_args_var .. ".push_back(" .. arg_val_code .. "); "
						end
					end
					args_code = "( [=]() mutable { std::vector<LuaValue> " .. temp_args_var .. "; " .. args_init_code .. "return " .. temp_args_var .. "; } )()"
				else
					local arg_list_str = translated_base -- self is first
					for i = 3, #node.ordered_children do
						local arg_val_code = get_single_lua_value_cpp_code(node.ordered_children[i], for_header, false, current_module_object_name)
						arg_list_str = arg_list_str .. ", " .. arg_val_code
					end
					args_code = "std::vector<LuaValue>{" .. arg_list_str .. "}"
				end

				return "call_lua_value(" .. func_access .. ", " .. args_code .. ")"
			end

		elseif node.type == "return_statement" then
			local expr_list_node = node.ordered_children[1]
			if expr_list_node and #expr_list_node.ordered_children > 0 then
				local return_values = {}
				for _, expr_node in ipairs(expr_list_node.ordered_children) do
					table.insert(return_values, translate_node_to_cpp(expr_node, for_header, false, current_module_object_name, depth + 1))
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
			local translated_operand = get_single_lua_value_cpp_code(operand_node, for_header, false, current_module_object_name)
			if operator == "-" then
				return "LuaValue(-get_double(" .. translated_operand .. "))"
			elseif operator == "not" then
				return "LuaValue(!is_lua_truthy(" .. translated_operand .. "))"
			elseif operator == "#" then
				return "lua_get_length(" .. translated_operand .. ")"
			elseif operator == "~" then
				return "LuaValue(static_cast<long long>(~get_long_long(" .. translated_operand .. ")))"
			else
				return operator .. translated_operand
			end
		elseif node.type == "expression_list" then
			local return_values = {}
			for _, expr_node in ipairs(node.ordered_children) do
				table.insert(return_values, translate_node_to_cpp(expr_node, for_header, false, current_module_object_name, depth + 1))
			end
			return "{" .. table.concat(return_values, ", ") .. "}"
		elseif node.type == "block" then
			local block_cpp_code = ""
			for _, child in ipairs(node.ordered_children) do
				block_cpp_code = block_cpp_code .. "    " .. translate_node_to_cpp(child, for_header, false, current_module_object_name, depth + 1) .. "\n"
			end
			return block_cpp_code
		else
			if node.type == "member_expression" then
				 local base_node = node.ordered_children[1]
				 local member_node = node.ordered_children[2]
				 local base_code = get_single_lua_value_cpp_code(base_node, for_header, false, current_module_object_name)
				 if base_node.type == "identifier" and (base_node.identifier == "math" or base_node.identifier == "string" or base_node.identifier == "table" or base_node.identifier == "os" or base_node.identifier == "io" or base_node.identifier == "package" or base_node.identifier == "coroutine") then
					return "get_object(_G->get_item(\"" .. base_node.identifier .. "\"))->get_item(\"" .. member_node.identifier .. "\")"
				 else
					return "lua_get_member(" .. base_code .. ", LuaValue(\"" .. member_node.identifier .. "\"))"
				 end
			elseif node.type == "table_index_expression" then
				 local base_node = node.ordered_children[1]
				 local index_node = node.ordered_children[2]
				 local translated_base = get_single_lua_value_cpp_code(base_node, for_header, false, current_module_object_name)
				 local translated_index = get_single_lua_value_cpp_code(index_node, for_header, false, current_module_object_name)
				 return "lua_get_member(" .. translated_base .. ", " .. translated_index .. ")"
			elseif node.type == "string" then
				 local s = node.value
				 s = s:gsub('\\', '\\\\'):gsub('"', '\\"'):gsub('\n', '\\n'):gsub('\t', '\\t'):gsub('\r', '\\r')
				 return "std::string(\"" .. s .. "\")"
			elseif node.type == "boolean" then
				 return "LuaValue(" .. tostring(node.value) .. ")"
			elseif node.type == "number" then
				 local num_str = tostring(node.value)
				 if not (string.find(num_str, "%.")) then return "LuaValue(static_cast<long long>(" .. num_str .. "))"
				 else return "LuaValue(" .. num_str .. ")" end
			elseif node.type == "integer" then
				 return "LuaValue(static_cast<long long>(" .. tostring(node.value) .. "))"
			elseif node.type == "while_statement" then
				local condition = get_single_lua_value_cpp_code(node.ordered_children[1], for_header, false, current_module_object_name)
				local body = translate_node_to_cpp(node.ordered_children[2], for_header, false, current_module_object_name, depth + 1)
				return "while (is_lua_truthy(" .. condition .. ")) {\n" .. body .. "}"
			elseif node.type == "repeat_until_statement" then
				local block_node = node.ordered_children[1]
				local condition_node = node.ordered_children[2]
				local cpp_code = "do {\n" .. translate_node_to_cpp(block_node, for_header, false, current_module_object_name, depth + 1)
				local condition_code = get_single_lua_value_cpp_code(condition_node, for_header, false, current_module_object_name)
				return cpp_code .. "} while (!is_lua_truthy(" .. condition_code .. "));\n"
			elseif node.type == "break_statement" then return "break;\n"
			elseif node.type == "label_statement" then return node.value .. ":;\n"
			elseif node.type == "goto_statement" then return "goto " .. node.value .. ";\n"
			elseif node.type == "for_numeric_statement" then
				local var_name = sanitize_cpp_identifier(node.ordered_children[1].identifier)
				local start_node = node.ordered_children[2]
				local end_node = node.ordered_children[3]
				local start_expr_code = get_single_lua_value_cpp_code(start_node, for_header, false, current_module_object_name)
				local end_expr_code = get_single_lua_value_cpp_code(end_node, for_header, false, current_module_object_name)
				local step_expr_code
				local body_node
				local step_node = nil
				if #node.ordered_children == 5 then
					step_node = node.ordered_children[4]
					step_expr_code = get_single_lua_value_cpp_code(step_node, for_header, false, current_module_object_name)
					body_node = node.ordered_children[5]
				else
					step_expr_code = nil
					body_node = node.ordered_children[4]
				end
				declared_variables[var_name] = true
				
				local all_integers = (start_node.type == "integer") and (end_node.type == "integer")
				local step_is_one = (step_node == nil) or (step_node.type == "integer" and tonumber(step_node.value) == 1)
				local step_is_neg_one = (step_node and step_node.type == "integer" and tonumber(step_node.value) == -1)
				local step_is_integer = (step_node == nil) or (step_node.type == "integer")
				
				if all_integers and step_is_one then
					local start_val = tostring(start_node.value)
					local end_val = tostring(end_node.value)
					return "for (long long " .. var_name .. "_native = " .. start_val .. "LL; " .. var_name .. "_native <= " .. end_val .. "LL; ++" .. var_name .. "_native) {\n" ..
						"LuaValue " .. var_name .. " = static_cast<long long>(" .. var_name .. "_native);\n" ..
						translate_node_to_cpp(body_node, for_header, false, current_module_object_name, depth + 1) .. "}"
				elseif all_integers and step_is_neg_one then
					local start_val = tostring(start_node.value)
					local end_val = tostring(end_node.value)
					return "for (long long " .. var_name .. "_native = " .. start_val .. "LL; " .. var_name .. "_native >= " .. end_val .. "LL; --" .. var_name .. "_native) {\n" ..
						"LuaValue " .. var_name .. " = static_cast<long long>(" .. var_name .. "_native);\n" ..
						translate_node_to_cpp(body_node, for_header, false, current_module_object_name, depth + 1) .. "}"
				elseif all_integers and step_is_integer then
					local start_val = tostring(start_node.value)
					local end_val = tostring(end_node.value)
					local step_val = tostring(step_node.value)
					local condition = "(" .. step_val .. " >= 0 ? " .. var_name .. "_native <= " .. end_val .. "LL : " .. var_name .. "_native >= " .. end_val .. "LL)"
					return "for (long long " .. var_name .. "_native = " .. start_val .. "LL; " .. condition .. "; " .. var_name .. "_native += " .. step_val .. "LL) {\n" ..
						"LuaValue " .. var_name .. " = static_cast<long long>(" .. var_name .. "_native);\n" ..
						translate_node_to_cpp(body_node, for_header, false, current_module_object_name, depth + 1) .. "}"
				else
					local actual_step = step_expr_code or "LuaValue(1.0)"
					local loop_condition
					if actual_step == "LuaValue(1.0)" or actual_step == "LuaValue(1)" or actual_step == "LuaValue(static_cast<long long>(1))" then
						loop_condition = "get_double(" .. var_name .. ") <= get_double(" .. end_expr_code .. ")"
					else
						loop_condition = "(get_double(" .. actual_step .. ") >= 0 ? get_double(" .. var_name .. ") <= get_double(" .. end_expr_code .. ") : get_double(" .. var_name .. ") >= get_double(" .. end_expr_code .. "))"
					end
					return "for (LuaValue " .. var_name .. " = " .. start_expr_code .. "; " .. loop_condition .. "; " .. var_name .. " = LuaValue(get_double(" .. var_name .. ") + get_double(" .. actual_step .. "))) {\n" .. translate_node_to_cpp(body_node, for_header, false, current_module_object_name, depth + 1) .. "}"
				end
			elseif node.type == "for_generic_statement" then
				local var_list_node = node.ordered_children[1]
				local expr_list_node = node.ordered_children[2]
				local body_node = node.ordered_children[3]
				local loop_vars = {}
				for _, var_node in ipairs(var_list_node.ordered_children) do
					local sanitized_var = sanitize_cpp_identifier(var_node.identifier)
					table.insert(loop_vars, sanitized_var)
					declared_variables[sanitized_var] = true
				end
				local iter_func_var = "iter_func_" .. self:get_unique_id()
				local iter_state_var = "iter_state_" .. self:get_unique_id()
				local iter_value_var = "iter_value_" .. self:get_unique_id()
				local results_var = "iter_results_" .. self:get_unique_id()
				local cpp_code = ""
				if #expr_list_node.ordered_children == 1 and (expr_list_node.ordered_children[1].type == "call_expression" or expr_list_node.ordered_children[1].type == "method_call_expression") then
					local iterator_call_code = translate_node_to_cpp(expr_list_node.ordered_children[1], for_header, false, current_module_object_name, depth + 1)
					cpp_code = cpp_code .. "std::vector<LuaValue> " .. results_var .. " = " .. iterator_call_code .. ";\n"
				else
					local iterator_values_code = translate_node_to_cpp(expr_list_node, for_header, false, current_module_object_name, depth + 1)
					cpp_code = cpp_code .. "std::vector<LuaValue> " .. results_var .. " = " .. iterator_values_code .. ";\n"
				end
				cpp_code = cpp_code .. "LuaValue " .. iter_func_var .. " = " .. results_var .. "[0];\n"
				cpp_code = cpp_code .. "LuaValue " .. iter_state_var .. " = " .. results_var .. "[1];\n"
				cpp_code = cpp_code .. "LuaValue " .. iter_value_var .. " = " .. results_var .. "[2];\n"
				cpp_code = cpp_code .. "while (true) {\n"
				cpp_code = cpp_code .. "    std::vector<LuaValue> args_obj = {" .. iter_state_var .. ", " .. iter_value_var .. "};\n"
				cpp_code = cpp_code .. "    std::vector<LuaValue> current_values = call_lua_value(" .. iter_func_var .. ", args_obj);\n"
				cpp_code = cpp_code .. "    if (current_values.empty() || std::holds_alternative<std::monostate>(current_values[0])) {\n"
				cpp_code = cpp_code .. "        break;\n"
				cpp_code = cpp_code .. "    }\n"
				for i, var_name in ipairs(loop_vars) do
					cpp_code = cpp_code .. "    LuaValue " .. var_name .. " = (current_values.size() > " .. (i - 1) .. ") ? current_values[" .. (i - 1) .. "] : std::monostate{};\n"
				end
				cpp_code = cpp_code .. "    " .. iter_value_var .. " = " .. loop_vars[1] .. ";\n"
				cpp_code = cpp_code .. translate_node_to_cpp(body_node, for_header, false, current_module_object_name, depth + 1) .. "\n"
				cpp_code = cpp_code .. "}\n"
				return cpp_code
			else
				print("WARNING: Unhandled node type: " .. node.type)
				return "/* UNHANDLED_NODE_TYPE: " .. node.type .. " */"
			end
		end
	end
	
	-- Main Function Wrapper
	local generated_code = translate_node_to_cpp(ast_root, for_header, false, current_module_object_name)
	
	local header = "#include <iostream>\n#include <vector>\n#include <string>\n#include <map>\n#include <memory>\n#include <variant>\n#include <functional>\n#include <cmath>\n#include \"lua_object.hpp\"\n\n"
	
	for module_name, _ in pairs(required_modules) do
		local sanitized_module_name = module_name:gsub("%.", "_")
		header = header .. "#include \"" .. sanitized_module_name .. ".hpp\"\n"
	end
	header = header .. "#include \"math.hpp\"\n"
	header = header .. "#include \"string.hpp\"\n"
	header = header .. "#include \"table.hpp\"\n"
	header = header .. "#include \"os.hpp\"\n"
	header = header .. "#include \"io.hpp\"\n"
	header = header .. "#include \"package.hpp\"\n"
	header = header .. "#include \"utf8.hpp\"\n"
	header = header .. "#include \"init.hpp\"\n"
	
	if is_main_script then
		if for_header then
			return header .. "\n// Main script header\n"
		else
			local main_function_start = "int main(int argc, char* argv[]) {\n" ..
										"init_G(argc, argv);\n"
			local main_function_end = "\n    return 0;\n}"
			return header .. main_function_start .. generated_code .. main_function_end
		end
	else 
		local namespace_start = "namespace " .. file_name .. " {\n"
		local namespace_end = "\n} // namespace " .. file_name .. "\n"
		if for_header then 
			local hpp_header = "#pragma once\n#include \"lua_object.hpp\"\n\n"
			local hpp_load_function_declaration = "std::vector<LuaValue> load();\n"
			local global_var_extern_declarations = ""
			for var_name, _ in pairs(module_global_vars) do
				global_var_extern_declarations = global_var_extern_declarations .. "extern LuaValue " .. var_name .. ";\n"
			end
			return hpp_header .. global_var_extern_declarations .. namespace_start .. hpp_load_function_declaration .. namespace_end
		else 
			local cpp_header = "#include \"" .. file_name .. ".hpp\"\n"
			local global_var_definitions = ""
			for var_name, _ in pairs(module_global_vars) do
				global_var_definitions = global_var_definitions .. "LuaValue " .. var_name .. ";\n"
			end
			local module_body_code = ""
			local module_identifier = ""
			local explicit_return_found = false
			local explicit_return_value = "std::make_shared<LuaObject>()" 

			for _, child in ipairs(ast_root.ordered_children) do
				if child.type == "local_declaration" and #child.ordered_children >= 2 and child.ordered_children[1].type == "variable" and child.ordered_children[2].type == "table_constructor" then
					module_identifier = child.ordered_children[1].identifier
					module_body_code = module_body_code .. "std::shared_ptr<LuaObject> " .. module_identifier .. " = std::make_shared<LuaObject>();\n"
				elseif child.type == "return_statement" then
					explicit_return_found = true
					local return_expr_node = child.ordered_children[1]
					if return_expr_node then
						if return_expr_node.type == "expression_list" then
							local return_values = {}
							for _, expr_node in ipairs(return_expr_node.ordered_children) do
								table.insert(return_values, translate_node_to_cpp(expr_node, false, false, current_module_object_name, 0))
							end
							explicit_return_value = table.concat(return_values, ", ")
						else
							explicit_return_value = translate_node_to_cpp(return_expr_node, false, false, current_module_object_name, 0)
						end
					else
						explicit_return_value = "std::monostate{}" 
					end 
				else
					module_body_code = module_body_code .. translate_node_to_cpp(child, false, false, current_module_object_name, 0) .. "\n"
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