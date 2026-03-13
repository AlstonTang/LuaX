--- START OF FILE Paste March 11, 2026 - 9:46AM ---

-- C++ Translator Module
-- Refactored for Void-Return / Output-Parameter Architecture
-- Optimized to reuse a single output buffer (_func_ret_buf) per function scope
-- Hoists side-effects to eliminate lambda wrappers for function calls
-- Fixes: std::monostate spam, argument vector detection, side-effect duplication, exponential translation blow-up

local CppTranslator = {}
CppTranslator.__index = CppTranslator

--------------------------------------------------------------------------------
-- C++ Reserved Keywords
--------------------------------------------------------------------------------

local cpp_keywords = {
	["operator"] = true, ["class"] = true, ["private"] = true, ["public"] = true,
	["protected"] = true, ["namespace"] = true, ["template"] = true, ["typename"] = true,
	["static"] = true, ["const"] = true, ["volatile"] = true, ["mutable"] = true,
	["auto"] = true, ["register"] = true, ["extern"] = true, ["inline"] = true,
	["virtual"] = true, ["explicit"] = true, ["friend"] = true, ["typedef"] = true,
	["union"] = true, ["enum"] = true, ["struct"] = true, ["sizeof"] = true,
	["new"] = true, ["delete"] = true, ["this"] = true,
	["nullptr"] = true, ["void"] = true,
	["int"] = true, ["char"] = true, ["short"] = true, ["long"] = true,
	["float"] = true, ["double"] = true, ["signed"] = true, ["unsigned"] = true,
	["switch"] = true, ["case"] = true, ["default"] = true,
	["using"] = true, ["try"] = true, ["catch"] = true,
	["throw"] = true, ["const_cast"] = true, ["static_cast"] = true,
	["dynamic_cast"] = true, ["reinterpret_cast"] = true,
	["_func_ret_buf"] = true, -- Reserved for internal buffer
	["args"] = true,
	["n_args"] = true,
	["out_result"] = true,
}

local function sanitize_cpp_identifier(name)
	if cpp_keywords[name] then
		return "lua_" .. name
	end
	return name
end

local function escape_cpp_string(s)
	return s:gsub('\\', '\\\\'):gsub('"', '\\"'):gsub('\n', '\\n'):gsub('\t', '\\t'):gsub('\r', '\\r')
end

--------------------------------------------------------------------------------
-- Global Constants
--------------------------------------------------------------------------------

local RET_BUF_NAME = "_func_ret_buf"
local empty_table = {}

--------------------------------------------------------------------------------
-- Global Lua Libraries (for special handling)
--------------------------------------------------------------------------------

local lua_global_libraries = {
	math = true, string = true, table = true, os = true,
	io = true, package = true, utf8 = true, debug = true,
	arg = true, coroutine = true
}

--------------------------------------------------------------------------------
-- TranslatorContext: Holds state during translation
--------------------------------------------------------------------------------

local TranslatorContext = {}
TranslatorContext.__index = TranslatorContext

function TranslatorContext:new(translator, for_header, current_module_object_name, is_main_script)
	local ctx = setmetatable({}, TranslatorContext)
	ctx.translator = translator
	ctx.for_header = for_header
	ctx.current_module_object_name = current_module_object_name
	ctx.is_main_script = is_main_script
	ctx.declared_variables = {}
	ctx.required_modules = {}
	ctx.module_global_vars = {}
	ctx.current_function_fixed_params_count = 0
	ctx.lib_member_caches = {}
	ctx.string_literals = {}
	ctx.string_counts = {}        -- Content -> Number of occurrences
	ctx.strings_to_cache = {}     -- Set of strings that passed the threshold
	ctx.global_identifier_caches = {}
	ctx.current_return_stmt = is_main_script and "goto luax_main_exit;" or "return out_result;"
	ctx.uses_ret_buf = false
	ctx.stmt_stack = {{}} 
	return ctx
end

function TranslatorContext:use_ret_buf()
	self.uses_ret_buf = true
	return RET_BUF_NAME
end

function TranslatorContext:get_unique_id()
	return self.translator:get_unique_id()
end

function TranslatorContext:save_scope()
	local saved = {}
	for k, v in pairs(self.declared_variables) do
		saved[k] = v
	end
	return saved
end

function TranslatorContext:restore_scope(saved)
	self.declared_variables = saved
end

function TranslatorContext:get_cpp_name(lua_name)
	if lua_name == "_" then
		return self.current_underscore_name or "_"
	end
	return sanitize_cpp_identifier(lua_name)
end

function TranslatorContext:declare_variable(name, info)
	local final_cpp_name
	if name == "_" then
		final_cpp_name = "lua_ignored_" .. self:get_unique_id()
		self.current_underscore_name = final_cpp_name
	else
		final_cpp_name = sanitize_cpp_identifier(name)
	end

	if type(info) == "string" then
		self.declared_variables[name] = { cpp_type = info, cpp_name = final_cpp_name }
	else
		local t = info or {}
		t.cpp_name = final_cpp_name
		self.declared_variables[name] = t
	end
	return final_cpp_name
end

function TranslatorContext:is_declared(name)
	return self.declared_variables[name]
end

function TranslatorContext:to_long_long(expr, tp)
	if tp == "long long" then return expr end
	if tp == "double" then return "static_cast<long long>(" .. expr .. ")" end
	if tp == "bool" then return "(" .. expr .. " ? 1LL : 0LL)" end
	return "get_long_long(" .. expr .. ")"
end

function TranslatorContext:to_double(expr, tp)
	if tp == "double" then return expr end
	if tp == "long long" then return "static_cast<double>(" .. expr .. ")" end
	if tp == "bool" then return "(" .. expr .. " ? 1.0 : 0.0)" end
	return "get_double(" .. expr .. ")"
end

function TranslatorContext:to_bool(expr, tp)
	if tp == "bool" then return expr end
	return "is_lua_truthy(" .. expr .. ")"
end

function TranslatorContext:getCppType(var_name, init_tp)
	local analysis = self.var_types and self.var_types[var_name]
	if not analysis then return "LuaValue" end

	local has_int = analysis["long long"]
	local has_double = analysis["double"]
	local has_bool = analysis["bool"]
	local has_str = analysis["std::string"] or analysis["std::string_view"]
	local has_other = analysis["LuaValue"]

	if not has_bool and not has_str and not has_other then
		if has_double then return "double" end
		return "long long"
	end

	if has_bool and not has_int and not has_double and not has_str and not has_other then
		return "bool"
	end

	return "LuaValue"
end

function TranslatorContext:is_reassigned(name)
	return self.reassigned_vars and self.reassigned_vars[name]
end

function TranslatorContext:get_variable_cpp_type(name)
	local info = self.declared_variables[name]
	if type(info) == "table" and info.cpp_type then
		return info.cpp_type
	end
	return "LuaValue"
end

function TranslatorContext:add_required_module(module_name)
	self.required_modules[module_name] = true
end

function TranslatorContext:add_module_global_var(var_name)
	self.module_global_vars[var_name] = true
end

function TranslatorContext:get_lib_cache(lib_name, member_name)
	local cache_key = lib_name .. "_" .. member_name
	if not self.lib_member_caches[cache_key] then
		self.lib_member_caches[cache_key] = "_cache_lib_" .. cache_key
		if not self.global_identifier_caches[lib_name] then
			self.global_identifier_caches[lib_name] = "_cache_global_" .. lib_name
		end
	end
	return self.lib_member_caches[cache_key]
end

function TranslatorContext:count_string(s)
	self.string_counts[s] = (self.string_counts[s] or 0) + 1
end

function TranslatorContext:get_string_expr(s)
	local count = self.string_counts[s] or 0
	
	if count > 1 then
		if not self.string_literals[s] then
			local safe_s = s:gsub("[^%a%d]", "_")
			if #safe_s > 15 then safe_s = safe_s:sub(1, 15) end
			self.string_literals[s] = "_cache_str_" .. safe_s .. "_" .. self:get_unique_id()
		end
		return self.string_literals[s]
	else
		return 'std::string_view("' .. escape_cpp_string(s) .. '")'
	end
end

function TranslatorContext:get_string_cache(s)
	if not self.string_literals[s] then
		local safe_s = s:gsub("[^%a%d]", "_")
		if #safe_s > 20 then safe_s = safe_s:sub(1, 20) end
		local id = self:get_unique_id()
		self.string_literals[s] = "_cache_str_" .. safe_s .. "_" .. id
	end
	return self.string_literals[s]
end

function TranslatorContext:get_global_cache(name)
	local count = self.string_counts[name] or 0
	
	if count > 1 then
		if not self.global_identifier_caches[name] then
			self.global_identifier_caches[name] = "_cache_global_" .. name
		end
		return self.global_identifier_caches[name]
	else
		return "_G->get_item(" .. self:get_string_expr(name) .. ")"
	end
end

function TranslatorContext:add_statement(stmt)
	local current_list = self.stmt_stack[#self.stmt_stack]
	table.insert(current_list, stmt)
end

function TranslatorContext:capture_start()
	table.insert(self.stmt_stack, {})
end

function TranslatorContext:capture_end()
	local stmts = table.remove(self.stmt_stack)
	return table.concat(stmts, "")
end

function TranslatorContext:flush_statements()
	local stmts = self.stmt_stack[#self.stmt_stack]
	local code = table.concat(stmts, "")
	self.stmt_stack[#self.stmt_stack] = {}
	return code
end

--------------------------------------------------------------------------------
-- Node Handlers Registry
--------------------------------------------------------------------------------

local NodeHandlers = {}

local function register_handler(node_type, handler)
	NodeHandlers[node_type] = handler
end

local function translate_typed_node(ctx, node, depth, opts)
	depth = depth or 0
	opts = opts or {}
	
	if depth > 1000 then
		error("Recursion limit exceeded in translate_node. Node type: " .. (node and node[1] or "nil"))
	end
	
	if not node then
		return "", "LuaValue"
	end
	
	if not node[1] then
		return "/* ERROR: Node with no type */", "LuaValue"
	end
	
	local handler = NodeHandlers[node[1]]
	if handler then
		local code, tp = handler(ctx, node, depth, opts)
		return code, tp or "LuaValue"
	else
		return "/* UNHANDLED_NODE_TYPE: " .. node[1] .. " */", "LuaValue"
	end
end

local function translate_node(ctx, node, depth, opts)
	local code, _ = translate_typed_node(ctx, node, depth, opts)
	return code
end

local function is_table_unpack_call(node)
	if node[1] == "call_expression" then
		local call_func = node[5][1]
		if call_func[1] == "member_expression" then
			local base = call_func[5][1]
			local member = call_func[5][2]
			if base[3] == "table" and member[3] == "unpack" then
				return true
			end
		end
	end
	return false
end

local function is_multiret(node)
	return node[1] == "call_expression" or 
		   node[1] == "method_call_expression" or
		   node[1] == "varargs"
end

local function build_args_code(ctx, node, start_index, depth, self_arg)
	local children = node[5]
	local count = #children
	local has_complex_args = false
	
	if count >= start_index then
		local last_arg = children[count]
		if is_multiret(last_arg) or is_table_unpack_call(last_arg) then
			has_complex_args = true
		end
	end
	
	if has_complex_args then
		local vec_var = "args_vec_" .. ctx:get_unique_id()
		ctx:add_statement("LuaValueVector " .. vec_var .. "; ")
		ctx:add_statement(vec_var .. ".reserve(" .. (count - start_index + 1 + (self_arg and 1 or 0)) .. ");\n")
		
		if self_arg then
			ctx:add_statement(vec_var .. ".push_back(" .. self_arg .. ");\n")
		end
		
		for i = start_index, count do
			local arg_node = children[i]
			if i == count and (is_multiret(arg_node) or is_table_unpack_call(arg_node)) then
				local ret_buf_name = translate_node(ctx, arg_node, depth + 1, { multiret = true })
				ctx:add_statement(vec_var .. ".insert(" .. vec_var .. ".end(), " .. ret_buf_name .. ".begin(), " .. ret_buf_name .. ".end());\n")
			else
				local arg_val = translate_node(ctx, arg_node, depth + 1)
				ctx:add_statement(vec_var .. ".push_back(" .. arg_val .. ");\n")
			end
		end
		
		return vec_var, true, (count - start_index + 1 + (self_arg and 1 or 0))
	else
		local arg_list = {}
		local arg_count = 0
		
		if self_arg then
			table.insert(arg_list, self_arg)
			arg_count = arg_count + 1
		end
		
		for i = start_index, count do
			local arg_val = translate_node(ctx, children[i], depth + 1)
			table.insert(arg_list, arg_val)
			arg_count = arg_count + 1
		end
		
		return table.concat(arg_list, ", "), false, arg_count
	end
end

local function translate_assignment_target(ctx, var_node, value_code, depth)
	if var_node[1] == "member_expression" then
		local base_node = var_node[5][1]
		local member_node = var_node[5][2]
		local translated_base = translate_node(ctx, base_node, depth + 1)
		local member_name = member_node[3]
		local member_cache_var = ctx:get_string_cache(member_name)
		return "get_object(" .. translated_base .. ")->set(" .. member_cache_var .. ", " .. value_code .. ");\n"
	elseif var_node[1] == "table_index_expression" then
		local base_node = var_node[5][1]
		local index_node = var_node[5][2]
		local translated_base = translate_node(ctx, base_node, depth + 1)
		local translated_index = translate_node(ctx, index_node, depth + 1)
		return "get_object(" .. translated_base .. ")->set_item(" .. translated_index .. ", " .. value_code .. ");\n"
	else
		local var_name = var_node[3]
		local declaration_prefix = ""
		local cpp_name
		
		if not ctx:is_declared(var_name) then
			cpp_name = ctx:declare_variable(var_name)
			
			if ctx.is_main_script then
				declaration_prefix = "LuaValue "
			else
				ctx:add_module_global_var(var_name)
			end
		else
			local decl = ctx:is_declared(var_name)
			cpp_name = decl.cpp_name or sanitize_cpp_identifier(var_name)
		end
		
		return declaration_prefix .. cpp_name .. " = " .. value_code .. ";\n"
	end
end

--------------------------------------------------------------------------------
-- Literal Handlers
--------------------------------------------------------------------------------

register_handler("string", function(ctx, node, depth)
	return ctx:get_string_expr(node[2]), "std::string_view"
end)

register_handler("number", function(ctx, node, depth)
	local num_str = tostring(node[2])
	if not string.find(num_str, "%.") then
		return num_str .. "LL", "long long"
	else
		return num_str, "double"
	end
end)

register_handler("integer", function(ctx, node, depth)
	return tostring(node[2]) .. "LL", "long long"
end)

register_handler("boolean", function(ctx, node, depth)
	return (node[2] == "true" and "true" or "false"), "bool"
end)

--------------------------------------------------------------------------------
-- Identifier Handler
--------------------------------------------------------------------------------

register_handler("identifier", function(ctx, node, depth)
	local decl_info = ctx:is_declared(node[3])
	if decl_info then
		local cpp_name = decl_info.cpp_name or sanitize_cpp_identifier(node[3])
		if decl_info.is_ptr then
			return "(*" .. decl_info.ptr_name .. ")", ctx:get_variable_cpp_type(node[3])
		end
		return cpp_name, ctx:get_variable_cpp_type(node[3])
	end
	
	if node[3] == "nil" then
		return "std::monostate{}"
	elseif node[3] == "true" then
		return "true", "bool"
	elseif node[3] == "false" then
		return "false", "bool"
	elseif lua_global_libraries[node[3]] then
		if ctx.overrides and ctx.overrides[node[3]] then
			return "get_object(_G->get(" .. ctx:get_string_cache(node[3]) .. "))"
		end
		return "get_object(" .. ctx:get_global_cache(node[3]) .. ")"
	elseif node[3] == "type" or node[3] == "print" or node[3] == "error" or node[3] == "tonumber" or node[3] == "tostring" or node[3] == "setmetatable" or node[3] == "getmetatable" or node[3] == "pairs" or node[3] == "ipairs" or node[3] == "next" or node[3] == "select" or node[3] == "rawget" or node[3] == "rawset" or node[3] == "rawequal" or node[3] == "pcall" or node[3] == "xpcall" or node[3] == "require" or node[3] == "loadfile" or node[3] == "dofile" or node[3] == "load" or node[3] == "assert" or node[3] == "collectgarbage" or node[3] == "_VERSION" or node[3] == "_G" then
		return ctx:get_global_cache(node[3])
	else
		return sanitize_cpp_identifier(node[3])
	end
end)

--------------------------------------------------------------------------------
-- Root and Block Handlers
--------------------------------------------------------------------------------

register_handler("Root", function(ctx, node, depth)
	local parts = {}
	for _, child in ipairs(node[5] or empty_table) do
		table.insert(parts, translate_node(ctx, child, depth + 1))
		table.insert(parts, "\n")
	end
	return table.concat(parts)
end)

register_handler("block", function(ctx, node, depth, opts)
	local parts = {}
	if not (opts and opts.no_braces) then table.insert(parts, "{\n") end
	
	local saved_scope = ctx:save_scope()
	for _, child in ipairs(node[5] or empty_table) do
		table.insert(parts, translate_node(ctx, child, depth + 1))
	end
	ctx:restore_scope(saved_scope)
	
	if not (opts and opts.no_braces) then table.insert(parts, "}\n") end
	return table.concat(parts)
end)

--------------------------------------------------------------------------------
-- Expression Handlers
--------------------------------------------------------------------------------

register_handler("binary_expression", function(ctx, node, depth)
	local operator = node[2]
	
	if (operator == "and" or operator == "or") then
		local left, left_type = translate_typed_node(ctx, node[5][1], depth + 1)
		local left_stmts = ctx:flush_statements()
		
		ctx:capture_start()
		local right, right_type = translate_typed_node(ctx, node[5][2], depth + 1)
		local right_stmts = ctx:capture_end()
		
		if left_stmts == "" and right_stmts == "" and left_type == "bool" and right_type == "bool" then
			return "(" .. left .. (operator == "and" and " && " or " || ") .. right .. ")", "bool"
		end
		
		local temp_var = "logic_res_" .. ctx:get_unique_id()
		ctx:add_statement(left_stmts)
		ctx:add_statement("LuaValue " .. temp_var .. " = " .. left .. ";\n")
		
		if operator == "and" then
			ctx:add_statement("if (is_lua_truthy(" .. temp_var .. ")) {\n")
		else
			ctx:add_statement("if (!is_lua_truthy(" .. temp_var .. ")) {\n")
		end
		
		ctx:add_statement(right_stmts)
		ctx:add_statement("    " .. temp_var .. " = " .. right .. ";\n")
		ctx:add_statement("}\n")
		
		return temp_var
	end

	if node[5][1][1] == "number" and node[5][2][1] == "number" then
		local l = tonumber(node[5][1][2])
		local r = tonumber(node[5][2][2])
		if l and r then
			if operator == "+" then return tostring(l + r)
			elseif operator == "-" then return tostring(l - r)
			elseif operator == "*" then return tostring(l * r)
			elseif operator == "/" and r ~= 0 then return tostring(l / r)
			end
		end
	elseif node[5][1][1] == "integer" and node[5][2][1] == "integer" then
		local l = tonumber(node[5][1][2])
		local r = tonumber(node[5][2][2])
		if l and r then
			if operator == "+" then return tostring(l + r) .. "LL"
			elseif operator == "-" then return tostring(l - r) .. "LL"
			elseif operator == "*" then return tostring(l * r) .. "LL"
			elseif operator == "//" and r ~= 0 then return tostring(math.floor(l / r)) .. "LL"
			elseif operator == "%" and r ~= 0 then return tostring(l % r) .. "LL"
			elseif operator == "&" then return tostring(l & r) .. "LL"
			elseif operator == "|" then return tostring(l | r) .. "LL"
			elseif operator == "~" then return tostring(l ~ r) .. "LL"
			elseif operator == "<<" then return tostring(l << r) .. "LL"
			elseif operator == ">>" then return tostring(l >> r) .. "LL"
			end
		end
	elseif operator == ".." and node[5][1][1] == "string" and node[5][2][1] == "string" then
		return ctx:get_string_cache(node[5][1][2] .. node[5][2][2])
	elseif operator == ".." then
		local chain = {}
		local current = node
		
		while current[1] == "binary_expression" and current[2] == ".." do
			table.insert(chain, current[5][1])
			current = current[5][2]
		end
		table.insert(chain, current)
		
		local all_strings = true
		local combined = ""
		for _, part in ipairs(chain) do
			if part[1] == "string" then
				combined = combined .. part[2]
			else
				all_strings = false
				break
			end
		end
		if all_strings then
			return ctx:get_string_cache(combined)
		end
	
		local translated_parts = {}
		for _, part in ipairs(chain) do
			local code, _ = translate_typed_node(ctx, part, depth + 1)
			table.insert(translated_parts, code)
		end
		
		local prev_stmts = ctx:flush_statements()
		if prev_stmts ~= "" then ctx:add_statement(prev_stmts) end
	
		return "lua_concat(" .. table.concat(translated_parts, ", ") .. ")", "LuaValue"
	end

	local left, left_type = translate_typed_node(ctx, node[5][1], depth + 1)
	local right, right_type = translate_typed_node(ctx, node[5][2], depth + 1)
	
	local math_type = "LuaValue"
	if (left_type == "long long" or left_type == "double") and 
	   (right_type == "long long" or right_type == "double") then
		math_type = (left_type == "double" or right_type == "double") and "double" or "long long"
	end
	
	local cmp_type = "lua"
	if math_type ~= "LuaValue" or (left_type == "bool" and right_type == "bool") then
		cmp_type = "native"
	end
	
	local prev_stmts = ctx:flush_statements()
	if prev_stmts ~= "" then ctx:add_statement(prev_stmts) end
	
	local is_bitwise = (operator == "&" or operator == "|" or operator == "~" or operator == "<<" or operator == ">>")
	if is_bitwise then
		if left_type ~= "long long" then left = ctx:to_long_long(left, left_type) end
		if right_type ~= "long long" then right = ctx:to_long_long(right, right_type) end
		math_type = "long long"
	else
		if math_type == "long long" then
			left = ctx:to_long_long(left, left_type)
			right = ctx:to_long_long(right, right_type)
		elseif math_type == "double" then
			left = ctx:to_double(left, left_type)
			right = ctx:to_double(right, right_type)
		end
	end

	if operator == "+" then
		return "(" .. left .. " + " .. right .. ")", math_type
	elseif operator == "-" then
		return "(" .. left .. " - " .. right .. ")", math_type
	elseif operator == "*" then
		return "(" .. left .. " * " .. right .. ")", math_type
	elseif operator == "/" then
		return "(" .. left .. " / " .. right .. ")", "double"
	elseif operator == "//" then
		return "static_cast<long long>(" .. left .. " / " .. right .. ")", "long long"
	elseif operator == "%" then
		return "(" .. left .. " % " .. right .. ")", "long long"
	elseif operator == "^" then
		return "std::pow(" .. left .. ", " .. right .. ")", "double"
	elseif operator == "&" then
		return "(" .. left .. " & " .. right .. ")", "long long"
	elseif operator == "|" then
		return "(" .. left .. " | " .. right .. ")", "long long"
	elseif operator == "~" then
		return "(" .. left .. " ^ " .. right .. ")", "long long"
	elseif operator == "<<" then
		return "(" .. left .. " << " .. right .. ")", "long long"
	elseif operator == ">>" then
		return "(" .. left .. " >> " .. right .. ")", "long long"
	elseif operator == "==" then
		if cmp_type == "native" then return "(" .. left .. " == " .. right .. ")", "bool" end
		return "lua_equals(" .. left .. ", " .. right .. ")", "bool"
	elseif operator == "~=" then
		if cmp_type == "native" then return "(" .. left .. " != " .. right .. ")", "bool" end
		return "(!lua_equals(" .. left .. ", " .. right .. "))", "bool"
	elseif operator == "<" then
		if cmp_type == "native" then return "(" .. left .. " < " .. right .. ")", "bool" end
		return "lua_less_than(" .. left .. ", " .. right .. ")", "bool"
	elseif operator == ">" then
		if cmp_type == "native" then return "(" .. left .. " > " .. right .. ")", "bool" end
		return "lua_greater_than(" .. left .. ", " .. right .. ")", "bool"
	elseif operator == "<=" then
		if cmp_type == "native" then return "(" .. left .. " <= " .. right .. ")", "bool" end
		return "lua_less_equals(" .. left .. ", " .. right .. ")", "bool"
	elseif operator == ">=" then
		if cmp_type == "native" then return "(" .. left .. " >= " .. right .. ")", "bool" end
		return "lua_greater_equals(" .. left .. ", " .. right .. ")", "bool"
	else
		return left .. " " .. operator .. " " .. right, "LuaValue"
	end
end)

register_handler("unary_expression", function(ctx, node, depth)
	local operator = node[2]
	local translated_operand, op_type = translate_typed_node(ctx, node[5][1], depth + 1)
	
	if operator == "-" then
		if op_type == "long long" or op_type == "double" then return "(-" .. translated_operand .. ")", op_type end
		return "(-" .. translated_operand .. ")", "LuaValue"
	elseif operator == "not" then
		if op_type == "bool" then return "(!" .. translated_operand .. ")", "bool" end
		return "(!is_lua_truthy(" .. translated_operand .. "))", "bool"
	elseif operator == "#" then
		return "get_long_long(lua_get_length(" .. translated_operand .. "))", "long long"
	elseif operator == "~" then
		if op_type == "long long" then return "(~" .. translated_operand .. ")", "long long" end
		return "(~" .. translated_operand .. ")", "LuaValue"
	else
		return operator .. " " .. translated_operand, "LuaValue"
	end
end)

register_handler("member_expression", function(ctx, node, depth)
	local base_node = node[5][1]
	local member_node = node[5][2]
	
	if base_node[1] == "identifier" and lua_global_libraries[base_node[3]] then
		if not (ctx.overrides and ctx.overrides[base_node[3]]) then
			if base_node[3] == "math" then
				if member_node[3] == "pi" then return "3.14159265358979323846", "double" end
				if member_node[3] == "huge" then return "std::numeric_limits<double>::infinity()", "double" end
			end
		end
	end
	
	local base_code = translate_node(ctx, base_node, depth + 1)
	local member_name = member_node[3]
	local member_expr = ctx:get_string_expr(member_name)
	
	return "lua_get_member(" .. base_code .. ", " .. member_expr .. ")"
end)

register_handler("table_index_expression", function(ctx, node, depth)
	local base_node = node[5][1]
	local index_node = node[5][2]
	local translated_base = translate_node(ctx, base_node, depth + 1)
	local translated_index = translate_node(ctx, index_node, depth + 1)
	return "lua_get_member(" .. translated_base .. ", " .. translated_index .. ")"
end)

register_handler("expression_list", function(ctx, node, depth)
	local return_values = {}
	for _, expr_node in ipairs(node[5] or empty_table) do
		table.insert(return_values, translate_node(ctx, expr_node, depth + 1))
	end
	return "{" .. table.concat(return_values, ", ") .. "}"
end)

register_handler("expression_statement", function(ctx, node, depth)
	local expr_node = node[5][1]
	local is_call = (expr_node[1] == "call_expression" or expr_node[1] == "method_call_expression")
	
	local code = translate_node(ctx, expr_node, depth + 1, { multiret = is_call })
	local stmts = ctx:flush_statements()
	
	if stmts ~= "" then
		if code == ctx:use_ret_buf() or code == "std::monostate{}" then
			return stmts
		else
			return stmts .. code .. ";\n"
		end
	else
		if code == "std::monostate{}" then
			return "" 
		end
		return code .. ";\n"
	end
end)

--------------------------------------------------------------------------------
-- Varargs Handler
--------------------------------------------------------------------------------

register_handler("varargs", function(ctx, node, depth, opts)
	local start_index = ctx.current_function_fixed_params_count
	
	if opts.multiret then
		ctx:add_statement(ctx:use_ret_buf() .. ".clear();\n")
		ctx:add_statement("if (n_args > " .. start_index .. ") [[likely]]" .. ctx:use_ret_buf() .. ".insert(" .. ctx:use_ret_buf() .. ".end(), args + " .. start_index .. ", args + n_args);\n")
		return ctx:use_ret_buf()
	else
		local temp_var = "vararg_" .. ctx:get_unique_id()
		ctx:add_statement("LuaValue " .. temp_var  .. ";\n")
		ctx:add_statement("if (n_args > " .. start_index .. ") [[likely]] " .. temp_var .. " = args[" .. start_index .. "]; else " .. temp_var .. "= LuaValue(std::monostate{});\n")
		return temp_var
	end
end)

--------------------------------------------------------------------------------
-- Table Constructor Handler
--------------------------------------------------------------------------------

register_handler("table_constructor", function(ctx, node, depth)
	local fields = node:get_all_children_of_type("table_field")
	local temp_table_var = "temp_table_" .. ctx:get_unique_id()
	
	if #fields == 0 then
		ctx:add_statement("auto " .. temp_table_var .. " = LuaObject::create();\n")
		return temp_table_var
	end

	local props = {}
	local array = {}
	local complex_statements = {}
	local has_complex = false
	local list_index = 1

	for _, field_node in ipairs(fields) do
		local key_child = field_node[5][1]
		local value_child = field_node[5][2]

		if value_child then
			local key_part
			if key_child[1] == "identifier" then
				key_part = '{LuaObject::intern("' .. key_child[3] .. '"), '
			else
				key_part = "{" .. translate_node(ctx, key_child, depth + 1) .. ", "
			end
			local value_part = translate_node(ctx, value_child, depth + 1)
			table.insert(props, key_part .. value_part .. "}")
		else
			value_child = key_child
			if is_multiret(value_child) or is_table_unpack_call(value_child) then
				has_complex = true
				table.insert(complex_statements, { "multiret", value_child, list_index })
			else
				local value_part = translate_node(ctx, value_child, depth + 1)
				table.insert(array, value_part)
				list_index = list_index + 1
			end
		end
	end

	if #props > 0 and #props <= 8 and #array == 0 and not has_complex then
		ctx:add_statement("auto " .. temp_table_var .. " = LuaObject::create({" .. table.concat(props, ", ") .. "});\n")
	elseif #props == 0 and #array > 0 and #array <= 8 and not has_complex then
		ctx:add_statement("auto " .. temp_table_var .. " = LuaObject::create({}, {" .. table.concat(array, ", ") .. "});\n")
	elseif #props > 0 and #props <= 8 and #array > 0 and #array <= 8 and not has_complex then
		ctx:add_statement("auto " .. temp_table_var .. " = LuaObject::create({" .. table.concat(props, ", ") .. "}, {" .. table.concat(array, ", ") .. "});\n")
	else
		local props_str = "{" .. table.concat(props, ", ") .. "}"
		local array_str = "{" .. table.concat(array, ", ") .. "}"
		ctx:add_statement("auto " .. temp_table_var .. " = LuaObject::create(" .. props_str .. ", " .. array_str .. ");\n")
	end

	if has_complex then
		for _, stmt in ipairs(complex_statements) do
			if stmt[1] == "multiret" then
				local varargs_buf = translate_node(ctx, stmt[2], depth + 1, { multiret = true })
				ctx:add_statement("for (size_t i = 0; i < " .. varargs_buf .. ".size(); ++i) {\n")
				ctx:add_statement("  " .. temp_table_var .. "->set_item(LuaValue(static_cast<long long>(" .. stmt[3] .. " + i)), " .. varargs_buf .. "[i]);\n")
				ctx:add_statement("}\n")
			end
		end
	end
	
	return temp_table_var
end)

--------------------------------------------------------------------------------
-- Call Expression Handlers
--------------------------------------------------------------------------------

local BuiltinCallHandlers = {}

BuiltinCallHandlers["print"] = function(ctx, node, depth, opts)
	local children = node[5]
	local count = #children
	
	for i = 2, count do
		local arg_node = children[i]
		
		if i == count and (is_multiret(arg_node) or is_table_unpack_call(arg_node)) then
			local ret_buf = translate_node(ctx, arg_node, depth + 1, { multiret = true })
			ctx:add_statement("for (size_t k = 0; k < " .. ret_buf .. ".size(); ++k) { ")
				ctx:add_statement("if (k > 0) std::cout << \"\\t\"; ")
			ctx:add_statement("print_value(" .. ret_buf .. "[k]); } ")
		else
			local translated_arg = translate_node(ctx, arg_node, depth + 1)
			ctx:add_statement("print_value(" .. translated_arg .. ");")
			if i < count then
				ctx:add_statement("std::cout << \"\\t\";")
			end
		end
	end
	ctx:add_statement("std::cout << std::endl;\n")
	
	if opts.multiret then
		ctx:add_statement(ctx:use_ret_buf() .. ".clear();\n")
		return ctx:use_ret_buf()
	else
		return "std::monostate{}"
	end
end

BuiltinCallHandlers["table.insert"] = function(ctx, node, depth, opts)
	local children = node[5]
	local count = #children
	if count == 3 then
		local table_code = translate_node(ctx, children[2], depth + 1)
		local value_code = translate_node(ctx, children[3], depth + 1)
		ctx:add_statement("lua_table_insert(" .. table_code .. ", " .. value_code .. ");\n")
		if opts.multiret then
			ctx:add_statement(ctx:use_ret_buf() .. ".clear();\n")
			return ctx:use_ret_buf()
		else
			return "std::monostate{}"
		end
	elseif count == 4 then
		local table_code = translate_node(ctx, children[2], depth + 1)
		local pos_code = translate_node(ctx, children[3], depth + 1)
		local value_code = translate_node(ctx, children[4], depth + 1)
		ctx:add_statement("lua_table_insert(" .. table_code .. ", static_cast<long long>(get_double(" .. pos_code .. ")), " .. value_code .. ");\n")
		if opts.multiret then
			ctx:add_statement(ctx:use_ret_buf() .. ".clear();\n")
			return ctx:use_ret_buf()
		else
			return "std::monostate{}"
		end
	end
	return nil
end

BuiltinCallHandlers["table.unpack"] = function(ctx, node, depth, opts)
	if not opts.multiret then return nil end
	local children = node[5]
	if #children >= 2 then
		local table_code = translate_node(ctx, children[2], depth + 1)
		ctx:add_statement(ctx:use_ret_buf() .. " = get_object(" .. table_code .. ")->array_part;\n")
		return ctx:use_ret_buf()
	end
	return nil
end

BuiltinCallHandlers["require"] = function(ctx, node, depth, opts)
	local module_name_node = node[5][2]
	if module_name_node and module_name_node[1] == "string" then
		local module_name = module_name_node[2]
		ctx:add_required_module(module_name)
		local sanitized_module_name = module_name:gsub("%.", "_")
		
		if opts.multiret then
			ctx:add_statement(ctx:use_ret_buf() .. " = " .. sanitized_module_name .. "::load();\n")
			return ctx:use_ret_buf()
		else
			return "get_return_value(" .. sanitized_module_name .. "::load(), 0)", "LuaValue"
		end
	end
	return "/* require call with non-string argument */"
end

BuiltinCallHandlers["setmetatable"] = function(ctx, node, depth, opts)
	local table_node = node[5][2]
	local metatable_node = node[5][3]
	local translated_table = translate_node(ctx, table_node, depth + 1)
	local translated_metatable = translate_node(ctx, metatable_node, depth + 1)
	
	ctx:add_statement("get_object(" .. translated_table .. ")->set_metatable(get_object(" .. translated_metatable .. "));\n")
	
	if opts.multiret then
		ctx:add_statement(ctx:use_ret_buf() .. ".clear(); " .. ctx:use_ret_buf() .. ".push_back(" .. translated_table .. ");\n")
		return ctx:use_ret_buf()
	else
		return translated_table
	end
end

local StringMethodHandlers = {}

local function handle_string_method(method_name, ctx, node, base_node, depth, opts)
	ctx:capture_start() 
	local base_code = translate_node(ctx, base_node, depth + 1)
	local pattern_code = translate_node(ctx, node[5][3], depth + 1)
	local replacement_code = (method_name == "gsub") and translate_node(ctx, node[5][4], depth + 1) or nil
	
	local sub_statements = ctx:capture_end()
	ctx:add_statement(sub_statements)

	local call_stmt = ""
	if method_name == "match" then
		call_stmt = "lua_string_match("..base_code..", "..pattern_code..", " .. ctx:use_ret_buf() .. ");\n"
	elseif method_name == "find" then
		call_stmt = "lua_string_find("..base_code..", "..pattern_code..", " .. ctx:use_ret_buf() .. ");\n"
	elseif method_name == "gsub" then
		call_stmt = "lua_string_gsub("..base_code..", "..pattern_code..", "..replacement_code..", " .. ctx:use_ret_buf() .. ");\n"
	end
	
	ctx:add_statement(ctx:use_ret_buf() .. ".clear();\n")
	ctx:add_statement(call_stmt)
	
	if opts.multiret then
		return ctx:use_ret_buf()
	else
		local temp_var = "str_res_" .. ctx:get_unique_id()
		ctx:add_statement("LuaValue " .. temp_var .. " = get_return_value(" .. ctx:use_ret_buf() .. ", 0);\n")
		return temp_var
	end
end

local function get_call_args(node)
	local list_args = {}
	local children = node[5]
	if children then
		for i = 2, #children do
			table.insert(list_args, children[i])
		end
	end
	return list_args
end

StringMethodHandlers["match"] = function(ctx, node, base_node, depth, opts) return handle_string_method("match", ctx, node, base_node, depth, opts) end
StringMethodHandlers["find"] = function(ctx, node, base_node, depth, opts) return handle_string_method("find", ctx, node, base_node, depth, opts) end
StringMethodHandlers["gsub"] = function(ctx, node, base_node, depth, opts) return handle_string_method("gsub", ctx, node, base_node, depth, opts) end

register_handler("call_expression", function(ctx, node, depth, opts)
	local func_node = node[5][1]
	
	if func_node[1] == "member_expression" then
		local base = func_node[5][1]
		local member = func_node[5][2]
		if base[3] == "string" or base[3] == "table" or base[3] == "math" or base[3] == "os" then
			local handler = BuiltinCallHandlers[base[3] .. "." .. member[3]]
			if handler then
				local result = handler(ctx, node, depth, opts)
				if result then return result end
			end
			if base[3] == "string" then
				local s_handler = StringMethodHandlers[member[3]]
				if s_handler then
					local s_result = s_handler(ctx, node, node[5][2], depth, opts)
					if s_result then return s_result end
				end
			end
		end
	end
	
	if func_node[1] == "identifier" then
		local handler = BuiltinCallHandlers[func_node[3]]
		if handler then
			local result = handler(ctx, node, depth, opts)
			if result then return result end
		end
	end
	
	local InlineLocalFunctions = {
		is_digit = "lua_is_digit",
		is_alpha = "lua_is_alpha",
		is_whitespace = "lua_is_whitespace",
		is_hex_digit = "lua_is_hex_digit",
		is_alnum = "lua_is_alnum"
	}
	
	if func_node[1] == "identifier" then
		local inline_func = InlineLocalFunctions[func_node[3]]
		if inline_func and ctx:is_declared(func_node[3]) then
			local arg_node = node[5][2]
			if arg_node then
				local arg_code = translate_node(ctx, arg_node, depth + 1)
				if opts.multiret then
					ctx:add_statement(ctx:use_ret_buf() .. ".clear(); " .. ctx:use_ret_buf() .. ".push_back(LuaValue(lua_to_bool(" .. inline_func .. "(" .. arg_code .. "))));\n")
					return ctx:use_ret_buf()
				else
					return inline_func .. "(" .. arg_code .. ")"
				end
			end
		end
	end
	
	local args_code, is_vector, num_args = build_args_code(ctx, node, 2, depth, nil)
	local translated_func_access
	local is_known_local = false
	if func_node[1] == "identifier" then
		local decl_info = ctx:is_declared(func_node[3])
		if decl_info then
			is_known_local = true
			if type(decl_info) == "table" and decl_info.is_ptr then
				translated_func_access = "(*" .. decl_info.ptr_name .. ")"
			else
				translated_func_access = sanitize_cpp_identifier(func_node[3])
			end
		elseif func_node[3] == "insert" and ctx:is_declared("table") then
			local func_cache_var = ctx:get_string_cache(func_node[3])
			translated_func_access = "_G->get(" .. func_cache_var .. ")"
		else
			local func_cache_var = ctx:get_string_cache(func_node[3])
			translated_func_access = "_G->get(" .. func_cache_var .. ")"
		end
	else
		translated_func_access = translate_node(ctx, func_node, depth + 1)
	end
	
	if not opts.multiret and not is_vector and num_args <= 3 then
		local temp_var = "call_res_" .. ctx:get_unique_id()
		local call_expr
		if is_known_local then
			call_expr = "get_callable(" .. translated_func_access .. ")->call" .. num_args .. "(" .. args_code .. ")"
		else
			call_expr = "lua_call" .. num_args .. "(" .. translated_func_access .. ", " .. ctx:use_ret_buf() .. (args_code ~= "" and (", " .. args_code) or "") .. ")"
		end
		if opts.no_temp then
			return call_expr, "LuaValue"
		end
		ctx:add_statement("LuaValue " .. temp_var .. " = " .. call_expr .. ";\n")
		return temp_var
	elseif is_vector then
		if is_known_local then
			ctx:add_statement(ctx:use_ret_buf() .. ".clear();\n")
			ctx:add_statement("get_callable(" .. translated_func_access .. ")->call(" .. args_code .. ".data(), " .. args_code .. ".size(), " .. ctx:use_ret_buf() .. ");\n")
		else
			ctx:add_statement("call_lua_value(" .. translated_func_access .. ", " .. args_code .. ".data(), " .. args_code .. ".size(), " .. ctx:use_ret_buf() .. ");\n")
		end
	else
		if args_code == "" then
			if is_known_local then
				ctx:add_statement(ctx:use_ret_buf() .. ".clear();\n")
				ctx:add_statement("get_callable(" .. translated_func_access .. ")->call(nullptr, 0, " .. ctx:use_ret_buf() .. ");\n")
			else
				ctx:add_statement("call_lua_value(" .. translated_func_access .. ", {}, 0, " .. ctx:use_ret_buf() .. ");\n")
			end
		else
			if is_known_local then
				local args_arr = "args_" .. ctx:get_unique_id()
				ctx:add_statement("const LuaValue " .. args_arr .. "[] = {" .. args_code .. "};\n")
				ctx:add_statement(ctx:use_ret_buf() .. ".clear();\n")
				ctx:add_statement("get_callable(" .. translated_func_access .. ")->call(" .. args_arr .. ", " .. num_args .. ", " .. ctx:use_ret_buf() .. ");\n")
			else
				ctx:add_statement("call_lua_value(" .. translated_func_access .. ", " .. ctx:use_ret_buf() .. ", " .. args_code .. ");\n")
			end
		end
	end
	
	if opts.multiret then
		return ctx:use_ret_buf()
	else
		local temp_var = "call_res_" .. ctx:get_unique_id()
		ctx:add_statement("LuaValue " .. temp_var .. " = get_return_value(" .. ctx:use_ret_buf() .. ", 0);\n")
		return temp_var
	end
end)

local MethodCallHandlers = {}

MethodCallHandlers["match"] = function(ctx, node, base_node, depth, opts) return handle_string_method("match", ctx, node, base_node, depth, opts) end
MethodCallHandlers["find"] = function(ctx, node, base_node, depth, opts) return handle_string_method("find", ctx, node, base_node, depth, opts) end
MethodCallHandlers["gsub"] = function(ctx, node, base_node, depth, opts) return handle_string_method("gsub", ctx, node, base_node, depth, opts) end

StringMethodHandlers["byte"] = function(ctx, node, base_node, depth, opts) return MethodCallHandlers["byte"](ctx, node, base_node, depth, opts) end
StringMethodHandlers["sub"] = function(ctx, node, base_node, depth, opts) return MethodCallHandlers["sub"](ctx, node, base_node, depth, opts) end

MethodCallHandlers["byte"] = function(ctx, node, base_node, depth, opts)
	local arg1 = node[5][3]
	local arg2 = node[5][4]
	local arg3 = node[5][5]
	
	if not arg1 then
		local base_code = translate_node(ctx, base_node, depth + 1)
		if opts.multiret then
			ctx:add_statement(ctx:use_ret_buf() .. ".clear();\n")
			ctx:add_statement("lua_string_byte(" .. base_code .. ", 1LL, 1LL, " .. ctx:use_ret_buf() .. ");\n")
			return ctx:use_ret_buf()
		else
			return "lua_string_byte_at_raw(" .. base_code .. ", 1LL)"
		end
	elseif arg1 and not arg2 then
		local arg1_code = translate_node(ctx, arg1, depth + 1)
		local base_code = translate_node(ctx, base_node, depth + 1)
		
		if opts.multiret then
			ctx:add_statement(ctx:use_ret_buf() .. ".clear();\n")
			ctx:add_statement("lua_string_byte(" .. base_code .. ", " .. arg1_code .. ", " .. arg1_code .. ", " .. ctx:use_ret_buf() .. ");\n")
			return ctx:use_ret_buf()
		else
			return "lua_string_byte_at_raw(" .. base_code .. ", " .. arg1_code .. ")"
		end
	elseif arg1 and arg2 and not arg3 then
		if arg1[1] == arg2[1] and arg1[2] == arg2[2] and (arg1[1] == "number" or arg1[1] == "integer") then
			local arg1_code = translate_node(ctx, arg1, depth + 1)
			local base_code = translate_node(ctx, base_node, depth + 1)
			if opts.multiret then
				ctx:add_statement(ctx:use_ret_buf() .. ".clear();\n")
				ctx:add_statement("lua_string_byte(" .. base_code .. ", " .. arg1_code .. ", " .. arg1_code .. ", " .. ctx:use_ret_buf() .. ");\n")
				return ctx:use_ret_buf()
			else
				return "lua_string_byte_at_raw(" .. base_code .. ", " .. arg1_code .. ")"
			end
		end
	end
	return nil
end

MethodCallHandlers["insert"] = function(ctx, node, base_node, depth, opts)
	local arg1 = node[5][3]
	local arg2 = node[5][4]
	
	if arg1 and not arg2 then
		local base_code = translate_node(ctx, base_node, depth + 1)
		local value_code = translate_node(ctx, arg1, depth + 1)
		ctx:add_statement("lua_table_insert(" .. base_code .. ", " .. value_code .. ");\n")
		if opts.multiret then
			ctx:add_statement(ctx:use_ret_buf() .. ".clear();\n")
			return ctx:use_ret_buf()
		else
			return "std::monostate{}"
		end
	end
	return nil
end

MethodCallHandlers["sub"] = function(ctx, node, base_node, depth, opts)
	local arg1 = node[5][3]
	local arg2 = node[5][4]
	local arg3 = node[5][5]
	
	if arg1 and arg2 and not arg3 then
		if arg1[1] == arg2[1] and arg1[2] == arg2[2] and (arg1[1] == "number" or arg1[1] == "integer") then
			local arg1_code = translate_node(ctx, arg1, depth + 1)
			local base_code = translate_node(ctx, base_node, depth + 1)
			if opts.multiret then
				ctx:add_statement(ctx:use_ret_buf() .. ".clear(); " .. ctx:use_ret_buf() .. ".push_back(lua_string_char_at(" .. base_code .. ", " .. arg1_code .. "));\n")
				return ctx:use_ret_buf()
			else
				return "lua_string_char_at(" .. base_code .. ", " .. arg1_code .. ")"
			end
		end

		local arg1_code = translate_node(ctx, arg1, depth + 1)
		local arg2_code = translate_node(ctx, arg2, depth + 1)
		local base_code = translate_node(ctx, base_node, depth + 1)
		if not opts.multiret then
			return "lua_string_sub(" .. base_code .. ", static_cast<long long>(get_double(" .. arg1_code .. ")), static_cast<long long>(get_double(" .. arg2_code .. ")))"
		end
	elseif arg1 and not arg2 then
		local arg1_code = translate_node(ctx, arg1, depth + 1)
		local base_code = translate_node(ctx, base_node, depth + 1)
		if not opts.multiret then
			return "lua_string_sub(" .. base_code .. ", static_cast<long long>(get_double(" .. arg1_code .. ")), -1)"
		end
	end
	return nil
end

register_handler("method_call_expression", function(ctx, node, depth, opts)
	local base_node = node[5][1]
	local method_node = node[5][2]
	local method_name = method_node[3]
	
	local handler = MethodCallHandlers[method_name]
	if handler then
		local res = handler(ctx, node, base_node, depth, opts)
		if res then return res end
	end
	
	local is_known_local_base = false
	if base_node[1] == "identifier" then
		local decl_info = ctx:is_declared(base_node[3])
		if decl_info then
			is_known_local_base = true
		end
	end
	
	local base_code = translate_node(ctx, base_node, depth + 1)
	local method_cache_var = ctx:get_string_cache(method_name)
	
	local args_code, is_vector, num_args = build_args_code(ctx, node, 3, depth, base_code)
	
	if not opts.multiret and not is_vector and num_args <= 3 then
		local temp_var = "mcall_res_" .. ctx:get_unique_id()
		local call_expr
		if is_known_local_base then
			call_expr = "get_callable(lua_get_member(" .. base_code .. ", " .. method_cache_var .. "))->call" .. num_args .. "(" .. (args_code ~= "" and args_code or "") .. ")"
		else
			call_expr = "lua_call" .. num_args .. "(lua_get_member(" .. base_code .. ", " .. method_cache_var .. "), " .. ctx:use_ret_buf() .. (args_code ~= "" and (", " .. args_code) or "") .. ")"
		end
		ctx:add_statement("LuaValue " .. temp_var .. " = " .. call_expr .. ";\n")
		return temp_var
	elseif is_vector then
		if is_known_local_base then
			ctx:add_statement(ctx:use_ret_buf() .. ".clear();\n")
			ctx:add_statement("get_callable(lua_get_member(" .. base_code .. ", " .. method_cache_var .. "))->call(" .. args_code .. ".data(), " .. args_code .. ".size(), " .. ctx:use_ret_buf() .. ");\n")
		else
			ctx:add_statement("call_lua_value(lua_get_member(" .. base_code .. ", " .. method_cache_var .. "), " .. args_code .. ".data(), " .. args_code .. ".size(), " .. ctx:use_ret_buf() .. ");\n")
		end
	else
		if is_known_local_base then
			local args_arr = "args_" .. ctx:get_unique_id()
			ctx:add_statement("const LuaValue " .. args_arr .. "[] = {" .. args_code .. "};\n")
			ctx:add_statement(ctx:use_ret_buf() .. ".clear();\n")
			ctx:add_statement("get_callable(lua_get_member(" .. base_code .. ", " .. method_cache_var .. "))->call(" .. args_arr .. ", " .. num_args .. ", " .. ctx:use_ret_buf() .. ");\n")
		else
			ctx:add_statement("call_lua_value(lua_get_member(" .. base_code .. ", " .. method_cache_var .. "), " .. ctx:use_ret_buf() .. ", " .. args_code .. ");\n")
		end
	end
	
	if opts.multiret then
		return ctx:use_ret_buf()
	else
		local temp_var = "mcall_res_" .. ctx:get_unique_id()
		ctx:add_statement("LuaValue " .. temp_var .. " = get_return_value(" .. ctx:use_ret_buf() .. ", 0);\n")
		return temp_var
	end
end)

--------------------------------------------------------------------------------
-- Declaration and Assignment Handlers
--------------------------------------------------------------------------------

register_handler("local_declaration", function(ctx, node, depth)
	local var_list_node = node[5][1]
	local expr_list_node = node[5][2]
	
	local num_vars = #(var_list_node[5] or empty_table)
	local num_exprs = expr_list_node and #(expr_list_node[5] or empty_table) or 0
	
	local cpp_code = ctx:flush_statements()
	
	if num_vars == 1 and num_exprs == 1 then
		local expr_node = expr_list_node[5][1]
		local var_node = var_list_node[5][1]
		local var_name = sanitize_cpp_identifier(var_node[3])

		if expr_node[1] == "call_expression" or expr_node[1] == "method_call_expression" then
			local val, tp = translate_typed_node(ctx, expr_node, depth + 1, { multiret = false, no_temp = true })
			local stmts = ctx:flush_statements()
			
			local var_name_lua = var_node[3]
			local cpp_type = ctx:getCppType(var_name_lua, tp)
			
			if cpp_type == "long long" then
				val = ctx:to_long_long(val, tp)
			elseif cpp_type == "double" then
				val = ctx:to_double(val, tp)
			elseif cpp_type == "bool" then
				val = ctx:to_bool(val, tp)
			end
			
			cpp_code = cpp_code .. stmts
			cpp_code = cpp_code .. cpp_type .. " " .. var_name .. " = " .. val .. ";\n"
			ctx:declare_variable(var_name_lua, cpp_type)
			return cpp_code
		elseif expr_node[1] == "table_constructor" then
			local val, tp = translate_typed_node(ctx, expr_node, depth + 1)
			local stmts = ctx:flush_statements()
			
			local pattern = "^auto " .. val .. " = (.-);\n$"
			local init_expr = stmts:match(pattern)
			
			if init_expr and not stmts:find(";\n.", 1, true) then 
				cpp_code = cpp_code .. "LuaValue " .. var_name .. " = " .. init_expr .. ";\n"
				ctx:declare_variable(var_node[3], "LuaValue")
				return cpp_code
			else
				cpp_code = cpp_code .. stmts
				cpp_code = cpp_code .. "LuaValue " .. var_name .. " = " .. val .. ";\n"
				ctx:declare_variable(var_node[3], "LuaValue")
				return cpp_code
			end
		end
	end
	
	local function_call_results_var = nil
	local has_function_call_expr = false
	local first_call_expr_index = -1
	
	for i = 1, num_exprs do
		local expr_node = expr_list_node[5][i]
		if (expr_node[1] == "call_expression" or expr_node[1] == "method_call_expression") and i == num_exprs and num_vars > i then
			has_function_call_expr = true
			first_call_expr_index = i
			break
		end
	end
	
	local init_values = {}
	local init_types = {}
	
	for i = 1, num_exprs do
		local expr_node = expr_list_node[5][i]
		if has_function_call_expr and i == first_call_expr_index then
			local ret_buf = translate_node(ctx, expr_node, depth + 1, { multiret = true })
			local stmts = ctx:flush_statements()
			cpp_code = cpp_code .. stmts
			function_call_results_var = ret_buf
		else
			local val, tp = translate_typed_node(ctx, expr_node, depth + 1)
			local stmts = ctx:flush_statements()
			cpp_code = cpp_code .. stmts
			init_values[i] = val
			init_types[i] = tp
		end
	end
	
	for i = 1, num_vars do
		local var_node = var_list_node[5][i]
		local var_name = sanitize_cpp_identifier(var_node[3])
		local initial_value_code = "std::monostate{}"
		local tp = "LuaValue"
		
		if i < first_call_expr_index or (not has_function_call_expr and i <= num_exprs) then
			initial_value_code = init_values[i]
			tp = init_types[i]
		elseif has_function_call_expr and i >= first_call_expr_index then
			local offset = i - first_call_expr_index
			initial_value_code = "get_return_value(" .. function_call_results_var .. ", " .. offset .. ")"
		end
		
		local cpp_type = ctx:getCppType(var_node[3], tp)
		
		if cpp_type == "long long" then
			initial_value_code = ctx:to_long_long(initial_value_code, tp)
		elseif cpp_type == "double" then
			initial_value_code = ctx:to_double(initial_value_code, tp)
		elseif cpp_type == "bool" then
			initial_value_code = ctx:to_bool(initial_value_code, tp)
		end
		
		cpp_code = cpp_code .. cpp_type .. " " .. var_name .. " = " .. initial_value_code .. ";\n"
		ctx:declare_variable(var_node[3], cpp_type)
	end
	
	return cpp_code
end)

register_handler("assignment", function(ctx, node, depth)
	local var_list_node = node[5][1]
	local expr_list_node = node[5][2]
	
	local num_vars = #(var_list_node[5] or empty_table)
	local num_exprs = #(expr_list_node[5] or empty_table)
	
	local cpp_code = ctx:flush_statements()
	
	local function_call_results_var = nil
	local has_function_call_expr = false
	local first_call_expr_index = -1
	
	for i = 1, num_exprs do
		local expr_node = expr_list_node[5][i]
		if (expr_node[1] == "call_expression" or expr_node[1] == "method_call_expression") and i == num_exprs and num_vars > i then
			has_function_call_expr = true
			first_call_expr_index = i
			break
		end
	end
	
	local values = {}
	local v_types = {}
	
	for i = 1, num_exprs do
		local expr_node = expr_list_node[5][i]
		if has_function_call_expr and i == first_call_expr_index then
			local ret_buf = translate_node(ctx, expr_node, depth + 1, { multiret = true })
			local stmts = ctx:flush_statements()
			cpp_code = cpp_code .. stmts
			function_call_results_var = ret_buf
		else
			local val, tp = translate_typed_node(ctx, expr_node, depth + 1)
			local stmts = ctx:flush_statements()
			cpp_code = cpp_code .. stmts
			values[i] = val
			v_types[i] = tp
		end
	end
	
	for i = 1, num_vars do
		local var_node = var_list_node[5][i]
		local value_code = "std::monostate{}"
		local tp = "LuaValue"
		
		if i < first_call_expr_index or (not has_function_call_expr and i <= num_exprs) then
			value_code = values[i]
			tp = v_types[i]
		elseif has_function_call_expr and i >= first_call_expr_index then
			local offset = i - first_call_expr_index
			value_code = "get_return_value(" .. function_call_results_var .. ", " .. offset .. ")"
		end
	
		local target_code
		if var_node[1] == "identifier" then
			local var_name = var_node[3]
			local decl = ctx:is_declared(var_name)
			
			if decl then
				local cpp_name = decl.cpp_name or sanitize_cpp_identifier(var_name)
				target_code = cpp_name .. " = " .. value_code .. ";\n"
			else
				target_code = translate_assignment_target(ctx, var_node, value_code, depth)
			end
		else
			target_code = translate_assignment_target(ctx, var_node, value_code, depth)
		end
		
		local stmts = ctx:flush_statements()
		cpp_code = cpp_code .. stmts .. target_code
	end
	
	return cpp_code
end)

--------------------------------------------------------------------------------
-- Function Declaration Handlers
--------------------------------------------------------------------------------

local function translate_function_body(ctx, node, depth)
	ctx:capture_start()

	local params_node = node[5][1]
	local body_node = node[5][2]
	
	local prev_param_count = ctx.current_function_fixed_params_count
	ctx.current_function_fixed_params_count = 0
	
	local saved_scope = ctx:save_scope()
	
	local params_extraction = ""
	local param_index_offset = 0
	local param_names = {}
	
	if node[1] == "method_declaration" or node.is_method then
		params_extraction = params_extraction .. "    LuaValue self; if (n_args > 0) [[likely]] self = args[0]; else LuaValue(std::monostate{});\n"
		ctx:declare_variable("self")
		param_index_offset = 1
		table.insert(param_names, "self")
	end
	
	ctx.current_function_fixed_params_count = param_index_offset
	
	for i = 1, #(params_node[5] or empty_table) do
		local param_node = params_node[5][i]
		if param_node[1] == "identifier" then
			local param_raw_name = param_node[3]
			local cpp_name = ctx:declare_variable(param_raw_name) 
			
			local vector_idx = i + param_index_offset - 1
			params_extraction = params_extraction .. "    LuaValue " .. cpp_name .. 
				"; if (n_args > " .. vector_idx .. ") [[likely]] " .. cpp_name .. 
				" = args[" .. vector_idx .. "]; else ".. cpp_name .." = LuaValue(std::monostate{});\n"
			
			table.insert(param_names, param_raw_name)
		end
	end
	
	local params_list = params_node[5] or empty_table
	local num_params = #params_list
	local arity = param_index_offset + num_params
	local has_vararg = false
	if params_node[5] then
		for _, p in ipairs(params_node[5]) do if p[1] == "varargs" then has_vararg = true break end end
	end

	local saved_return_stmt = ctx.current_return_stmt
	local saved_specialized_mode = ctx.specialized_return_mode
	
	local saved_uses_ret_buf = ctx.uses_ret_buf
	ctx.uses_ret_buf = false
	ctx.specialized_return_mode = false
	ctx.current_return_stmt = "return;"
	local body_code = translate_node(ctx, body_node, depth + 1, { no_braces = true })
	local body_stmts = ctx:capture_end()
	
	local buffer_decl = ctx.uses_ret_buf and ("    LuaRetBufGuard _ret_buf_guard; LuaValueVector& _func_ret_buf = _ret_buf_guard.buf;\n") or ""
	local var_lambda = "[=](const LuaValue* args, size_t n_args, LuaValueVector& out_result) mutable -> void {\n" .. buffer_decl .. params_extraction .. body_code .. body_stmts .. "}"
	
	local spec_lambda = nil
	local body_children = body_node[5] or empty_table
	local is_simple_return = (#body_children == 1 and body_children[1][1] == "return_statement")
	if arity <= 3 and not has_vararg and is_simple_return then
		ctx:restore_scope(saved_scope)
		ctx:capture_start()
		
		if node[1] == "method_declaration" or node.is_method then
			ctx:declare_variable("self")
		end

		local spec_params_list = ""
		local spec_params_extraction = ""
		for i = 1, arity do
			local p_name = param_names[i]
			local cpp_p_name = ctx:declare_variable(p_name)
			spec_params_list = spec_params_list .. (i > 1 and ", " or "") .. "const LuaValue& __a" .. i
			spec_params_extraction = spec_params_extraction .. "    LuaValue " .. cpp_p_name .. " = __a" .. i .. ";\n"
		end
		
		ctx.uses_ret_buf = false
		ctx.specialized_return_mode = true
		
		local spec_body_code = translate_node(ctx, body_node, depth + 1, { no_braces = true })
		local spec_body_stmts = ctx:capture_end()
		
		local spec_buffer_decl = ctx.uses_ret_buf and ("    LuaRetBufGuard _ret_buf_guard; LuaValueVector& _func_ret_buf = _ret_buf_guard.buf;\n") or ""
		
		local terminal_return = ""
		if not spec_body_code:match("return%s+[^;]+;") then
			terminal_return = "\n    return LuaValue(std::monostate{});\n"
		end
		
		spec_lambda = "[=](" .. spec_params_list .. ") mutable -> LuaValue {\n" .. 
					spec_buffer_decl .. spec_params_extraction .. 
					spec_body_code .. spec_body_stmts .. 
					terminal_return .. "}"
	end

	ctx.uses_ret_buf = saved_uses_ret_buf
	ctx.specialized_return_mode = saved_specialized_mode
	ctx.current_return_stmt = saved_return_stmt
	ctx:restore_scope(saved_scope)
	ctx.current_function_fixed_params_count = prev_param_count
	
	return var_lambda, spec_lambda, arity
end

register_handler("function_expression", function(ctx, node, depth)
	local var_lambda, spec_lambda, arity = translate_function_body(ctx, node, depth)
	if spec_lambda then
		return "make_specialized_callable<" .. arity .. ">(" .. var_lambda .. ", " .. spec_lambda .. ")"
	else
		return "make_lua_callable(" .. var_lambda .. ")"
	end
end)

register_handler("function_declaration", function(ctx, node, depth)
	local ptr_name = nil
	
	if node:meta().is_local and node[3] then
		ptr_name = node[3] .. "_ptr_" .. ctx:get_unique_id()
		local sanitized_var_name = sanitize_cpp_identifier(node[3])
		ctx:declare_variable(sanitized_var_name, { is_ptr = true, ptr_name = ptr_name })
	end
	
	local var_lambda, spec_lambda, arity = translate_function_body(ctx, node, depth)
	local callable_expr
	if spec_lambda then
		callable_expr = "make_specialized_callable<" .. arity .. ">(" .. var_lambda .. ", " .. spec_lambda .. ")"
	else
		callable_expr = "make_lua_callable(" .. var_lambda .. ")"
	end
	
	if node:meta().is_local and node[3] then
		local var_name = sanitize_cpp_identifier(node[3])
		ctx:declare_variable(node[3])
	end
	
	if node:meta().method_name  ~= nil then
		local prev_stmts = ctx:flush_statements()
		return prev_stmts .. "get_object(" .. sanitize_cpp_identifier(node[3]) .. ")->set(\"" .. node:meta().method_name .. "\", " .. callable_expr .. ");\n"
	elseif node[3] ~= nil then
		local prev_stmts = ctx:flush_statements()
		if node:meta().is_local  then
			local var_name = sanitize_cpp_identifier(node[3])
			return prev_stmts .. "auto " .. ptr_name .. " = std::make_shared<LuaValue>();\n" ..
				"*" .. ptr_name .. " = " .. callable_expr .. ";\n" ..
				"LuaValue " .. var_name .. " = *" .. ptr_name .. ";\n"
		else
			return prev_stmts .. "_G->set(\"" .. node[3] .. "\", " .. callable_expr .. ");\n"
		end
	else
		return callable_expr
	end
end)

register_handler("method_declaration", function(ctx, node, depth)
	local var_lambda, spec_lambda, arity = translate_function_body(ctx, node, depth)
	local callable_expr
	if spec_lambda then
		callable_expr = "make_specialized_callable<" .. arity .. ">(" .. var_lambda .. ", " .. spec_lambda .. ")"
	else
		callable_expr = "make_lua_callable(" .. var_lambda .. ")"
	end
	
	local prev_stmts = ctx:flush_statements()
	
	if node:meta().method_name ~= nil then
		return prev_stmts .. "get_object(" .. sanitize_cpp_identifier(node[3]) .. ")->set(\"" .. node:meta().method_name .. "\", " .. callable_expr .. ");\n"
	else
		return prev_stmts .. callable_expr .. "\n"
	end
end)

--------------------------------------------------------------------------------
-- Control Flow Handlers
--------------------------------------------------------------------------------

register_handler("if_statement", function(ctx, node, depth)
	local cpp_code = ctx:flush_statements()
	local open_count = 0
	
	for i, clause in ipairs(node[5] or empty_table) do
		if clause[1] == "if_clause" then
			ctx:capture_start()
			local cond, tp = translate_typed_node(ctx, clause[5][1], depth + 1)
			local pre = ctx:capture_end()
			local body = translate_node(ctx, clause[5][2], depth + 1, { no_braces = true })
			
			local cond_expr = (tp == "bool") and cond or ("is_lua_truthy(" .. cond .. ")")
			if pre ~= "" then
				cpp_code = cpp_code .. "{\n" .. pre .. "if (" .. cond_expr .. ") {\n" .. body .. "}"
				open_count = open_count + 1
			else
				cpp_code = cpp_code .. "if (" .. cond_expr .. ") {\n" .. body .. "}"
			end
		elseif clause[1] == "elseif_clause" then
			ctx:capture_start()
			local cond, tp = translate_typed_node(ctx, clause[5][1], depth + 1)
			local pre = ctx:capture_end()
			local body = translate_node(ctx, clause[5][2], depth + 1, { no_braces = true })
			
			local cond_expr = (tp == "bool") and cond or ("is_lua_truthy(" .. cond .. ")")
			if pre ~= "" then
				cpp_code = cpp_code .. " else {\n" .. pre .. "if (" .. cond_expr .. ") {\n" .. body .. "}"
				open_count = open_count + 1
			else
				cpp_code = cpp_code .. " else if (" .. cond_expr .. ") {\n" .. body .. "}"
			end
		elseif clause[1] == "else_clause" then
			local body = translate_node(ctx, clause[5][1], depth + 1, { no_braces = true })
			cpp_code = cpp_code .. " else {\n" .. body .. "}"
		end
	end
	
	for k=1, open_count do
		cpp_code = cpp_code .. "\n}"
	end
	
	return cpp_code .. "\n"
end)

register_handler("while_statement", function(ctx, node, depth)
	local prev_stmts = ctx:flush_statements()
	
	ctx:capture_start()
	local condition, tp = translate_typed_node(ctx, node[5][1], depth + 1)
	local pre_stmts = ctx:capture_end()
	
	local body = translate_node(ctx, node[5][2], depth + 1, { no_braces = true })
	
	if pre_stmts ~= "" then
		local cond_expr = (tp == "bool") and ("!" .. condition) or ("!is_lua_truthy(" .. condition .. ")")
		return prev_stmts .. "while (true) {\n" .. pre_stmts .. "if (" .. cond_expr .. ") break;\n" .. body .. "}\n"
	else
		local cond_expr = (tp == "bool") and condition or ("is_lua_truthy(" .. condition .. ")")
		return prev_stmts .. "while (" .. cond_expr .. ") {\n" .. body .. "}\n"
	end
end)

register_handler("repeat_until_statement", function(ctx, node, depth)
	local prev_stmts = ctx:flush_statements()
	
	local block_node = node[5][1]
	local condition_node = node[5][2]
	
	local body = translate_node(ctx, block_node, depth + 1, { no_braces = true })
	
	ctx:capture_start()
	local condition, tp = translate_typed_node(ctx, condition_node, depth + 1)
	local pre_stmts = ctx:capture_end()
	
	if pre_stmts ~= "" then
		local cond_expr = (tp == "bool") and condition or ("is_lua_truthy(" .. condition .. ")")
		return prev_stmts .. "do {\n" .. body .. pre_stmts .. "if (" .. cond_expr .. ") break;\n} while (true);\n"
	else
		local cond_expr = (tp == "bool") and ("!" .. condition) or ("!is_lua_truthy(" .. condition .. ")")
		return prev_stmts .. "do {\n" .. body .. "} while (" .. cond_expr .. ");\n"
	end
end)

register_handler("break_statement", function(ctx, node, depth)
	return ctx:flush_statements() .. "break;\n"
end)

register_handler("label_statement", function(ctx, node, depth)
	return ctx:flush_statements() .. node[2] .. ":;\n"
end)

register_handler("goto_statement", function(ctx, node, depth)
	return ctx:flush_statements() .. "goto " .. node[2] .. ";\n"
end)

register_handler("return_statement", function(ctx, node, depth)
	local expr_list_node = node[5] and node[5][1]
	local cpp_code = ctx:flush_statements()
	
	if expr_list_node and #(expr_list_node[5] or empty_table) > 0 then
		if ctx.specialized_return_mode then
			local expr_node = expr_list_node[5][1]
			local val = translate_node(ctx, expr_node, depth + 1)
			local stmts = ctx:flush_statements()
			return cpp_code .. stmts .. "return " .. val .. ";\n"
		else
			local num_exprs = #(expr_list_node[5] or empty_table)
			for i, expr_node in ipairs(expr_list_node[5] or empty_table) do
				local is_last = (i == num_exprs)
				local val = translate_node(ctx, expr_node, depth + 1, { multiret = is_last })
				local stmts = ctx:flush_statements()
				if is_last and val == RET_BUF_NAME then
					cpp_code = cpp_code .. stmts .. "out_result.insert(out_result.end(), " .. RET_BUF_NAME .. ".begin(), " .. RET_BUF_NAME .. ".end());\n"
				else
					cpp_code = cpp_code .. stmts .. "out_result.push_back(" .. val .. ");\n"
				end
			end
		end
	end
	return cpp_code .. ctx.current_return_stmt .. "\n"
end)

--------------------------------------------------------------------------------
-- For Loop Handlers
--------------------------------------------------------------------------------

register_handler("for_numeric_statement", function(ctx, node, depth)
	local prev_stmts = ctx:flush_statements()
	
	local var_raw_name = node[5][1][3]
	local var_name = sanitize_cpp_identifier(var_raw_name)
	local start_node = node[5][2]
	local end_node = node[5][3]
	
	local step_node = nil
	local body_node = nil
	if #(node[5] or empty_table) == 5 then
		step_node = node[5][4]
		body_node = node[5][5]
	else
		body_node = node[5][4]
	end

	local start_val, start_type = translate_typed_node(ctx, start_node, depth + 1)
	local start_stmts = ctx:flush_statements()
	
	local end_val, end_type = translate_typed_node(ctx, end_node, depth + 1)
	local end_stmts = ctx:flush_statements()
	
	local step_val, step_type = "1LL", "long long"
	if step_node then
		step_val, step_type = translate_typed_node(ctx, step_node, depth + 1)
	end
	local step_stmts = ctx:flush_statements()

	local is_integer_loop = true
	if step_node and step_node[1] == "number" then
		is_integer_loop = false
	elseif step_type == "double" or start_type == "double" or end_type == "double" then
		is_integer_loop = false
	end
	
	local known_step = nil
	if not step_node then 
		known_step = 1 
	elseif step_node[1] == "integer" then 
		known_step = tonumber(step_node[2]) 
	end

	local loop_id = ctx:get_unique_id()
	local stop_var = "limit_" .. loop_id
	local step_var = "step_" .. loop_id
	
	local cpp_type = is_integer_loop and "long long" or "double"
	ctx:declare_variable(var_raw_name, cpp_type)

	local start_cpp = is_integer_loop and ctx:to_long_long(start_val, start_type) or ctx:to_double(start_val, start_type)
	local end_cpp = is_integer_loop and ctx:to_long_long(end_val, end_type) or ctx:to_double(end_val, end_type)
	local step_cpp = is_integer_loop and ctx:to_long_long(step_val, step_type) or ctx:to_double(step_val, step_type)

	local cpp_block = "{\n"
	if start_stmts ~= "" then cpp_block = cpp_block .. start_stmts end
	if end_stmts ~= "" then cpp_block = cpp_block .. end_stmts end
	if step_stmts ~= "" then cpp_block = cpp_block .. step_stmts end
	
	cpp_block = cpp_block .. "    const " .. cpp_type .. " " .. stop_var .. " = " .. end_cpp .. ";\n"
	cpp_block = cpp_block .. "    const " .. cpp_type .. " " .. step_var .. " = " .. step_cpp .. ";\n"
	
	local comparison = ""
	if known_step then
		if known_step > 0 then
			comparison = var_name .. " <= " .. stop_var
		else
			comparison = var_name .. " >= " .. stop_var
		end
	else
		comparison = "(" .. step_var .. " >= 0 ? " .. var_name .. " <= " .. stop_var .. " : " .. var_name .. " >= " .. stop_var .. ")"
	end
	
	cpp_block = cpp_block .. "    for (" .. cpp_type .. " " .. var_name .. " = " .. start_cpp .. "; " .. comparison .. "; " .. var_name .. " += " .. step_var .. ") {\n"
	
	local inner_body = translate_node(ctx, body_node, depth + 1, { no_braces = true })
	cpp_block = cpp_block .. inner_body .. "    }\n}\n"
	
	return prev_stmts .. cpp_block
end)

register_handler("for_generic_statement", function(ctx, node, depth)
	local prev_stmts = ctx:flush_statements()
	
	local var_list_node = node[5][1]
	local expr_list_node = node[5][2]
	local body_node = node[5][3]
	local loop_vars = {}
	
	for _, var_node in ipairs(var_list_node[5] or empty_table) do
		local cpp_var_name = ctx:declare_variable(var_node[3])
		table.insert(loop_vars, cpp_var_name)
	end
	
	local iter_func_var = "iter_func_" .. ctx:get_unique_id()
	local iter_state_var = "iter_state_" .. ctx:get_unique_id()
	local iter_value_var = "iter_value_" .. ctx:get_unique_id()
	local results_var = "iter_results_" .. ctx:get_unique_id()
	local current_vals_var = "current_values_" .. ctx:get_unique_id()
	
	local cpp_code = prev_stmts .. "{\n"
	
	local is_ipairs = false
	local is_pairs = false
	local table_to_iter = nil

	if #(expr_list_node[5] or empty_table) == 1 then
		local call_node = expr_list_node[5][1]
		if call_node[1] == "call_expression" and call_node[5][1][1] == "identifier" then
			local func_name = call_node[5][1][3]
			if func_name == "ipairs" then
				if not ctx.overrides or not ctx.overrides[func_name] then
					is_ipairs = true
				end
			end
		end
	end

	if is_ipairs and #loop_vars <= 2 then
		local call_node = expr_list_node[5][1]
		table_to_iter = translate_node(ctx, call_node[5][2], depth + 1)
		local stmts = ctx:flush_statements()
		cpp_code = cpp_code .. stmts
		
		local idx_var = "i_" .. ctx:get_unique_id()
		local t_obj = "t_obj_" .. ctx:get_unique_id()
		cpp_code = cpp_code .. "auto " .. t_obj .. " = get_object(" .. table_to_iter .. ");\n"
		cpp_code = cpp_code .. "for (size_t " .. idx_var .. " = 0; " .. idx_var .. " < " .. t_obj .. "->array_part.size(); ++" .. idx_var .. ") {\n"
		if #loop_vars >= 1 then
			cpp_code = cpp_code .. "    LuaValue " .. loop_vars[1] .. " = static_cast<long long>(" .. idx_var .. " + 1);\n"
		end
		if #loop_vars >= 2 then
			cpp_code = cpp_code .. "    LuaValue " .. loop_vars[2] .. " = " .. t_obj .. "->array_part[" .. idx_var .. "];\n"
		end
		cpp_code = cpp_code .. "    if (std::holds_alternative<std::monostate>(" .. (#loop_vars >= 2 and loop_vars[2] or (t_obj .. "->array_part[" .. idx_var .. "]")) .. ")) break;\n"
		cpp_code = cpp_code .. translate_node(ctx, body_node, depth + 1, { no_braces = true }) .. "\n"
		cpp_code = cpp_code .. "}\n"
		cpp_code = cpp_code .. "}\n"
		return cpp_code
	end

	if #(expr_list_node[5] or empty_table) == 1 and (expr_list_node[5][1][1] == "call_expression" or expr_list_node[5][1][1] == "method_call_expression") then
		local iterator_call_buf = translate_node(ctx, expr_list_node[5][1], depth + 1, { multiret = true })
		local stmts = ctx:flush_statements()
		cpp_code = cpp_code .. stmts
		cpp_code = cpp_code .. "const LuaValueVector& " .. results_var .. " = " .. iterator_call_buf .. ";\n"
	else
		local iterator_values_code = translate_node(ctx, expr_list_node, depth + 1)
		local stmts = ctx:flush_statements()
		cpp_code = cpp_code .. stmts
		cpp_code = cpp_code .. "LuaValueVector " .. results_var .. " = " .. iterator_values_code .. ";\n"
	end
	
	cpp_code = cpp_code .. "LuaValue " .. iter_func_var .. " = " .. results_var .. "[0];\n"
	cpp_code = cpp_code .. "LuaValue " .. iter_state_var .. " = " .. results_var .. "[1];\n"
	cpp_code = cpp_code .. "LuaValue " .. iter_value_var .. " = " .. results_var .. "[2];\n"
	
	cpp_code = cpp_code .. "LuaValueVector " .. current_vals_var .. ";\n"
	cpp_code = cpp_code .. current_vals_var .. ".reserve(3);\n"
	
	cpp_code = cpp_code .. "while (true) {\n"
	cpp_code = cpp_code .. "    LuaValue args_obj[2] = {" .. iter_state_var .. ", " .. iter_value_var .. "};\n"
	
	cpp_code = cpp_code .. "    call_lua_value(" .. iter_func_var .. ", args_obj, 2, " .. current_vals_var .. ");\n"
	
	cpp_code = cpp_code .. "    if (" .. current_vals_var .. ".empty() || std::holds_alternative<std::monostate>(" .. current_vals_var .. "[0])) {\n"
	cpp_code = cpp_code .. "        break;\n"
	cpp_code = cpp_code .. "    }\n"
	
	for i, var_cpp_name in ipairs(loop_vars) do
		cpp_code = cpp_code .. "    LuaValue " .. var_cpp_name .. " = (" .. 
				current_vals_var .. ".size() > " .. (i - 1) .. ") ? " .. 
				current_vals_var .. "[" .. (i - 1) .. "] : std::monostate{};\n"
	end
	
	cpp_code = cpp_code .. "    " .. iter_value_var .. " = " .. loop_vars[1] .. ";\n"
	cpp_code = cpp_code .. translate_node(ctx, body_node, depth + 1, { no_braces = true }) .. "\n"
	cpp_code = cpp_code .. "}}\n"
	
	return cpp_code
end)

--------------------------------------------------------------------------------
-- CppTranslator Class Methods
--------------------------------------------------------------------------------

local function emit_cache_declarations(ctx)
	local global_code = ""
	local local_code = ""

	local sorted_strings = {}
	for s, _ in pairs(ctx.string_literals) do table.insert(sorted_strings, s) end
	table.sort(sorted_strings)
	for _, s in ipairs(sorted_strings) do
		local var = ctx.string_literals[s]
		global_code = global_code .. "static const LuaValue " .. var .. " = std::string_view(\"" .. escape_cpp_string(s) .. "\");\n"
	end

	local sorted_globals = {}
	for name, _ in pairs(ctx.global_identifier_caches) do table.insert(sorted_globals, name) end
	table.sort(sorted_globals)
	for _, name in ipairs(sorted_globals) do
		local var = ctx.global_identifier_caches[name]
		local_code = local_code .. "static const LuaValue " .. var .. " = _G->get_item(\"" .. name .. "\");\n"
	end

	return global_code, local_code
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

--------------------------------------------------------------------------------
-- Analysis: Override Detection
--------------------------------------------------------------------------------

local function analyze_overrides(ast, ctx)
	local overrides = { math = false, string = false, os = false }
	local reassigned_vars = {}
	local var_types = {}
	ctx.string_counts = {}

	local function infer_node_type(node, current_var_types)
		if not node then return "LuaValue" end
		local tag = node[1]
		
		if tag == "integer" then return "long long"
		elseif tag == "number" then return "double"
		elseif tag == "boolean" then return "bool"
		elseif tag == "string" then return "std::string_view"
		
		elseif tag == "identifier" then
			local known = current_var_types[node[3]]
			if known then
				local count = 0
				local last_t
				for t, _ in pairs(known) do count = count + 1; last_t = t end
				if count == 1 then return last_t end
			end
			return "LuaValue"
	
		elseif tag == "binary_expression" then
			local left = infer_node_type(node[5][1], current_var_types)
			local right = infer_node_type(node[5][2], current_var_types)
			local op = node[2]
	
			if (left == "long long" or left == "double") and (right == "long long" or right == "double") then
				if op == "/" or op == "^" then return "double" end
				if left == "double" or right == "double" then return "double" end
				return "long long"
			end
			if op == ".." then return "std::string" end
			if op == "==" or op == "<" or op == ">" then return "bool" end
		end
		
		return "LuaValue"
	end

	local function scan(node)
		if not node or type(node) ~= "table" then return end
		
		if node[1] == "string" then
			local s = node[2]
			ctx.string_counts[s] = (ctx.string_counts[s] or 0) + 1
		
		elseif node[1] == "identifier" then
			local name = node[3]
			if not ctx:is_declared(name) then
				ctx.string_counts[name] = (ctx.string_counts[name] or 0) + 1
			end

		elseif node[1] == "member_expression" or node[1] == "method_call_expression" then
			local member_node = node[5][2]
			if member_node and member_node[3] then
				local s = member_node[3]
				ctx.string_counts[s] = (ctx.string_counts[s] or 0) + 1
			end
		end

		local children = node[5]
		if children then
			for i = 1, #children do scan(children[i]) end
		end
	end
	
	scan(ast)
	ctx.overrides = overrides
	ctx.reassigned_vars = reassigned_vars
	ctx.var_types = var_types
end

--------------------------------------------------------------------------------
-- Math Library Inlining
--------------------------------------------------------------------------------

local MathHandlers = {
	abs = "std::abs", ceil = "std::ceil", floor = "std::floor",
	sin = "std::sin", cos = "std::cos", tan = "std::tan",
	asin = "std::asin", acos = "std::acos", atan = "std::atan",
	exp = "std::exp", log = "std::log", sqrt = "std::sqrt",
	deg = function(arg) return "(" .. arg .. " * 180.0 / 3.14159265358979323846)" end,
	rad = function(arg) return "(" .. arg .. " * 3.14159265358979323846 / 180.0)" end
}

for name, cpp_func in pairs(MathHandlers) do
	BuiltinCallHandlers["math." .. name] = function(ctx, node, depth, opts)
		if ctx.overrides and ctx.overrides.math then return nil end
		
		local call_args = get_call_args(node)
		if #call_args == 1 then
			local arg_code = translate_node(ctx, call_args[1], depth + 1)
			local expr
			if type(cpp_func) == "function" then
				expr = cpp_func(ctx:to_double(arg_code))
			else
				expr = cpp_func .. "(" .. ctx:to_double(arg_code) .. ")"
			end
			
			if opts.multiret then
				ctx:add_statement(ctx:use_ret_buf() .. ".clear(); " .. ctx:use_ret_buf() .. ".push_back(" .. expr .. ");\n")
				return ctx:use_ret_buf()
			else
				return expr
			end
		end
		return nil
	end
end

BuiltinCallHandlers["math.random"] = function(ctx, node, depth, opts)
	if ctx.overrides and ctx.overrides.math then return nil end
	local call_args = get_call_args(node)
	if #call_args == 0 then
		local expr = "(static_cast<double>(std::rand()) / RAND_MAX)"
		if opts.multiret then
			ctx:add_statement(ctx:use_ret_buf() .. ".clear(); " .. ctx:use_ret_buf() .. ".push_back(" .. expr .. ");\n")
			return ctx:use_ret_buf()
		else
			return expr
		end
	end
	return nil
end

BuiltinCallHandlers["math.max"] = function(ctx, node, depth, opts)
	if ctx.overrides and ctx.overrides.math then return nil end
	local call_args = get_call_args(node)
	if #call_args == 2 then
		local a = translate_node(ctx, call_args[1], depth + 1)
		local b = translate_node(ctx, call_args[2], depth + 1)
		local da = ctx:to_double(a)
		local db = ctx:to_double(b)
		local expr = "(" .. da .. " > " .. db .. " ? " .. da .. " : " .. db .. ")"
		if opts.multiret then
			ctx:add_statement(ctx:use_ret_buf() .. ".clear(); " .. ctx:use_ret_buf() .. ".push_back(" .. expr .. ");\n")
			return ctx:use_ret_buf()
		else
			return expr
		end
	end
	return nil
end

BuiltinCallHandlers["math.min"] = function(ctx, node, depth, opts)
	if ctx.overrides and ctx.overrides.math then return nil end
	local call_args = get_call_args(node)
	if #call_args == 2 then
		local a = translate_node(ctx, call_args[1], depth + 1)
		local b = translate_node(ctx, call_args[2], depth + 1)
		local da = ctx:to_double(a)
		local db = ctx:to_double(b)
		local expr = "(" .. da .. " < " .. db .. " ? " .. da .. " : " .. db .. ")"
		if opts.multiret then
			ctx:add_statement(ctx:use_ret_buf() .. ".clear(); " .. ctx:use_ret_buf() .. ".push_back(" .. expr .. ");\n")
			return ctx:use_ret_buf()
		else
			return expr
		end
	end
	return nil
end

--------------------------------------------------------------------------------
-- String Library Inlining
--------------------------------------------------------------------------------

BuiltinCallHandlers["string.byte"] = function(ctx, node, depth, opts)
	if ctx.overrides and ctx.overrides.string then return nil end
	local call_args = get_call_args(node)
	if #call_args == 1 then
		local str = translate_node(ctx, call_args[1], depth + 1)
		local expr = "lua_string_byte_at_raw(" .. str .. ", 1)"
		if opts.multiret then
			ctx:add_statement(ctx:use_ret_buf() .. ".clear();\n")
			ctx:add_statement("{\n    long long _b = " .. expr .. ";\n")
			ctx:add_statement("    if (_b != -1) " .. ctx:use_ret_buf() .. ".push_back(static_cast<double>(_b));\n")
			ctx:add_statement("}\n")
			return ctx:use_ret_buf()
		else
			return "lua_string_byte_at(" .. str .. ", 1)"
		end
	end
	return nil
end

BuiltinCallHandlers["string.len"] = function(ctx, node, depth, opts)
	if ctx.overrides and ctx.overrides.string then return nil end
	local call_args = get_call_args(node)
	if #call_args == 1 then
		local str = translate_node(ctx, call_args[1], depth + 1)
		local expr = "lua_get_length_int(" .. str .. ")"
		if opts.multiret then
			ctx:add_statement(ctx:use_ret_buf() .. ".clear(); " .. ctx:use_ret_buf() .. ".push_back(" .. expr .. ");\n")
			return ctx:use_ret_buf()
		else
			return expr
		end
	end
	return nil
end

BuiltinCallHandlers["string.char"] = function(ctx, node, depth, opts)
	if ctx.overrides and ctx.overrides.string then return nil end
	local call_args = get_call_args(node)
	if #call_args == 1 then
		local arg = translate_node(ctx, call_args[1], depth + 1)
		local expr = "LuaValue(std::string(1, static_cast<char>(get_double(" .. arg .. "))))"
		if opts.multiret then
			ctx:add_statement(ctx:use_ret_buf() .. ".clear(); " .. ctx:use_ret_buf() .. ".push_back(" .. expr .. ");\n")
			return ctx:use_ret_buf()
		else
			return expr
		end
	end
	return nil
end

--------------------------------------------------------------------------------
-- OS Library Inlining
--------------------------------------------------------------------------------

BuiltinCallHandlers["os.clock"] = function(ctx, node, depth, opts)
	if ctx.overrides and ctx.overrides.os then return nil end
	local expr = "(static_cast<double>(std::clock()) / CLOCKS_PER_SEC)"
	if opts.multiret then
		ctx:add_statement(ctx:use_ret_buf() .. ".clear(); " .. ctx:use_ret_buf() .. ".push_back(" .. expr .. ");\n")
		return ctx:use_ret_buf()
	else
		return expr
	end
end

BuiltinCallHandlers["os.time"] = function(ctx, node, depth, opts)
	if ctx.overrides and ctx.overrides.os then return nil end
	local call_args = get_call_args(node)
	if #call_args == 0 then
		local expr = "static_cast<long long>(std::time(nullptr))"
		if opts.multiret then
			ctx:add_statement(ctx:use_ret_buf() .. ".clear(); " .. ctx:use_ret_buf() .. ".push_back(" .. expr .. ");\n")
			return ctx:use_ret_buf()
		else
			return expr
		end
	end
	return nil
end

--------------------------------------------------------------------------------
-- Helpers
--------------------------------------------------------------------------------

function TranslatorContext:to_double(expr)
	return "to_double(" .. expr .. ")"
end

function CppTranslator:translate_recursive(ast_root, file_name, for_header, current_module_object_name, is_main_script)
	self.unique_id_counter = 0
	local ctx = TranslatorContext:new(self, for_header, current_module_object_name, is_main_script)
	
	if not for_header then
		analyze_overrides(ast_root, ctx)
	end

	local generated_code = translate_node(ctx, ast_root, 0)
	
	local remaining_stmts = ctx:flush_statements()
	generated_code = remaining_stmts .. generated_code
	
	local header = "#include <iostream>\n#include <vector>\n#include <string>\n#include <map>\n#include <memory>\n#include <variant>\n#include <functional>\n#include <cmath>\n#include <ctime>\n#include <cstdlib>\n#include <limits>\n#include \"lua_object.hpp\"\n\n"
	
	for module_name, _ in pairs(ctx.required_modules) do
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
			local global_cache_decls, local_cache_decls = emit_cache_declarations(ctx)
			local buffer_decl = "    LuaValueVector out_result; out_result.reserve(10);\n    LuaRetBufGuard _ret_buf_guard; LuaValueVector& _func_ret_buf = _ret_buf_guard.buf;\n"
			local main_function_start = "int main(int argc, char* argv[]) {\n" ..
										"init_G(argc, argv);\n" .. local_cache_decls .. buffer_decl
			local main_function_end = "\n    goto luax_main_exit;\nluax_main_exit:\n    luax_cleanup();\n    return 0;\n}"
			return header .. global_cache_decls .. main_function_start .. generated_code .. main_function_end
		end
	else
		local namespace_start = "namespace " .. file_name .. " {\n"
		local namespace_end = "\n} // namespace " .. file_name .. "\n"
		
		if for_header then
			local hpp_header = "#ifndef LUAX_" .. string.upper(file_name) .. "_HPP\n#define LUAX_" .. string.upper(file_name) .. "_HPP\n\n#include \"lua_object.hpp\"\n\n"
			local hpp_load_function_declaration = "LuaValueVector load();\n"
			local global_var_extern_declarations = ""
			for var_name, _ in pairs(ctx.module_global_vars) do
				global_var_extern_declarations = global_var_extern_declarations .. "extern LuaValue " .. var_name .. ";\n"
			end
			return hpp_header .. global_var_extern_declarations .. namespace_start .. hpp_load_function_declaration .. namespace_end .. "#endif\n"
		else
			local cpp_header = "#include \"" .. file_name .. ".hpp\"\n"
			local global_var_definitions = ""
			for var_name, _ in pairs(ctx.module_global_vars) do
				global_var_definitions = global_var_definitions .. "LuaValue " .. var_name .. ";\n"
			end
			
			local module_identifier = ""
			local explicit_return_found = false
			
			for _, child in ipairs(ast_root[5] or empty_table) do
				if child[1] == "local_declaration" and #(child[5] or empty_table) >= 2 and child[5][1][1] == "variable" and child[5][2][1] == "table_constructor" then
					module_identifier = child[5][1][3]
				elseif child[1] == "return_statement" then
					explicit_return_found = true
				end
			end
			
			local load_function_body = generated_code
			
			if not explicit_return_found then
				if module_identifier ~= "" then
					load_function_body = load_function_body .. "    out_result.push_back(" .. sanitize_cpp_identifier(module_identifier) .. ");\n"
				else
					load_function_body = load_function_body .. "    out_result.push_back(LuaObject::create());\n"
				end
				load_function_body = load_function_body .. "    return out_result;\n"
			end
			
			local global_cache_decls, local_cache_decls = emit_cache_declarations(ctx)
			local buffer_decl = "    LuaValueVector out_result; out_result.reserve(10);\n    LuaRetBufGuard _ret_buf_guard; LuaValueVector& _func_ret_buf = _ret_buf_guard.buf;\n"
			local load_function_definition = "LuaValueVector load() {\n" .. local_cache_decls .. buffer_decl .. load_function_body .. "}\n"
			return header .. cpp_header .. global_cache_decls .. global_var_definitions .. namespace_start .. load_function_definition .. namespace_end
		end
	end
end

return CppTranslator