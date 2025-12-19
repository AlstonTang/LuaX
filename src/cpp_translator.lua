-- C++ Translator Module
-- Refactored for Void-Return / Output-Parameter Architecture
-- Optimized to reuse a single output buffer (_func_ret_buf) per function scope
-- Hoists side-effects to eliminate lambda wrappers for function calls
-- Fixes: std::monostate spam, argument vector detection, side-effect duplication

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
	["_func_ret_buf"] = true -- Reserved for internal buffer
}

local function sanitize_cpp_identifier(name)
	if cpp_keywords[name] then
		return "lua_" .. name
	end
	return name
end

--------------------------------------------------------------------------------
-- Global Constants
--------------------------------------------------------------------------------

local RET_BUF_NAME = "_func_ret_buf"

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
	
	-- Stack of statement lists for hoisting side effects
	ctx.stmt_stack = {{}} 
	return ctx
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

function TranslatorContext:declare_variable(name, info)
	self.declared_variables[name] = info or true
end

function TranslatorContext:is_declared(name)
	return self.declared_variables[name]
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
		self.lib_member_caches[cache_key] = "_cache_" .. cache_key
	end
	return self.lib_member_caches[cache_key]
end

-- Statement Hoisting Methods
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
	-- Clear the current list
	self.stmt_stack[#self.stmt_stack] = {}
	return code
end

--------------------------------------------------------------------------------
-- Node Handlers Registry
--------------------------------------------------------------------------------

local NodeHandlers = {}

-- Register a handler for a node type
local function register_handler(node_type, handler)
	NodeHandlers[node_type] = handler
end

--------------------------------------------------------------------------------
-- Core Translation Function
--------------------------------------------------------------------------------

local function translate_node(ctx, node, depth, opts)
	depth = depth or 0
	opts = opts or {}
	
	if depth > 50 then
		print("ERROR: Recursion limit exceeded in translate_node. Node type: " .. (node and node.type or "nil"))
		os.exit(1)
	end
	
	if not node then
		return ""
	end
	
	if not node.type then
		print("ERROR: Node has no type field:", node)
		return "/* ERROR: Node with no type */"
	end
	
	local handler = NodeHandlers[node.type]
	if handler then
		return handler(ctx, node, depth, opts)
	else
		print("WARNING: Unhandled node type: " .. node.type)
		return "/* UNHANDLED_NODE_TYPE: " .. node.type .. " */"
	end
end

--------------------------------------------------------------------------------
-- Helper: Check if node is a table.unpack call
--------------------------------------------------------------------------------

local function is_table_unpack_call(node)
	if node.type == "call_expression" then
		local call_func = node.ordered_children[1]
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

--------------------------------------------------------------------------------
-- Helper: Check if node returns multiple values (call, varargs matches)
--------------------------------------------------------------------------------

local function is_multiret(node)
	return node.type == "call_expression" or 
		   node.type == "method_call_expression" or
		   node.type == "varargs"
end

--------------------------------------------------------------------------------
-- Helper: Build Arguments Code
-- Returns: code_string, is_vector_boolean
--------------------------------------------------------------------------------

local function build_args_code(ctx, node, start_index, depth, self_arg)
	local children = node.ordered_children
	local count = #children
	local has_complex_args = false
	
	-- Check if the LAST argument is multiret (call or varargs)
	if count >= start_index then
		local last_arg = children[count]
		if is_multiret(last_arg) or is_table_unpack_call(last_arg) then
			has_complex_args = true
		end
	end
	
	if has_complex_args then
		local vec_var = "args_vec_" .. ctx:get_unique_id()
		ctx:add_statement("std::vector<LuaValue> " .. vec_var .. "; ")
		ctx:add_statement(vec_var .. ".reserve(" .. (count - start_index + 1 + (self_arg and 1 or 0)) .. ");\n")
		
		if self_arg then
			ctx:add_statement(vec_var .. ".push_back(" .. self_arg .. ");\n")
		end
		
		for i = start_index, count do
			local arg_node = children[i]
			if i == count and (is_multiret(arg_node) or is_table_unpack_call(arg_node)) then
				-- Expand last argument
				local ret_buf_name = translate_node(ctx, arg_node, depth + 1, { multiret = true })
				ctx:add_statement(vec_var .. ".insert(" .. vec_var .. ".end(), " .. ret_buf_name .. ".begin(), " .. ret_buf_name .. ".end());\n")
			else
				-- Single value
				local arg_val = translate_node(ctx, arg_node, depth + 1)
				ctx:add_statement(vec_var .. ".push_back(" .. arg_val .. ");\n")
			end
		end
		
		return vec_var, true -- Return variable name and True indicating it's a vector
	else
		-- Simple comma-separated list
		local arg_list = {}
		
		if self_arg then
			table.insert(arg_list, self_arg)
		end
		
		for i = start_index, count do
			-- Normal translation returns a temporary variable name or literal
			local arg_val = translate_node(ctx, children[i], depth + 1)
			table.insert(arg_list, arg_val)
		end
		
		return table.concat(arg_list, ", "), false -- Return list string and False
	end
end

--------------------------------------------------------------------------------
-- Helper: Handle Variable Assignment Target
--------------------------------------------------------------------------------

local function translate_assignment_target(ctx, var_node, value_code, depth)
	if var_node.type == "member_expression" then
		local base_node = var_node.ordered_children[1]
		local member_node = var_node.ordered_children[2]
		local translated_base = translate_node(ctx, base_node, depth + 1)
		local member_name = member_node.identifier
		return "get_object(" .. translated_base .. ")->set(\"" .. member_name .. "\", " .. value_code .. ");\n"
	elseif var_node.type == "table_index_expression" then
		local base_node = var_node.ordered_children[1]
		local index_node = var_node.ordered_children[2]
		local translated_base = translate_node(ctx, base_node, depth + 1)
		local translated_index = translate_node(ctx, index_node, depth + 1)
		return "get_object(" .. translated_base .. ")->set_item(" .. translated_index .. ", " .. value_code .. ");\n"
	else
		local var_code = translate_node(ctx, var_node, depth + 1)
		local declaration_prefix = ""
		if var_node.type == "identifier" and not ctx:is_declared(var_node.identifier) then
			if ctx.is_main_script then
				declaration_prefix = "LuaValue "
			else
				ctx:add_module_global_var(var_node.identifier)
			end
			ctx:declare_variable(var_node.identifier)
		end
		return declaration_prefix .. var_code .. " = " .. value_code .. ";\n"
	end
end

--------------------------------------------------------------------------------
-- Literal Handlers
--------------------------------------------------------------------------------

register_handler("string", function(ctx, node, depth)
	local s = node.value
	s = s:gsub('\\', '\\\\'):gsub('"', '\\"'):gsub('\n', '\\n'):gsub('\t', '\\t'):gsub('\r', '\\r')
	return "std::string(\"" .. s .. "\")"
end)

register_handler("number", function(ctx, node, depth)
	local num_str = tostring(node.value)
	if not string.find(num_str, "%.") then
		return "LuaValue(static_cast<long long>(" .. num_str .. "))"
	else
		return "LuaValue(" .. num_str .. ")"
	end
end)

register_handler("integer", function(ctx, node, depth)
	return "LuaValue(static_cast<long long>(" .. tostring(node.value) .. "))"
end)

register_handler("boolean", function(ctx, node, depth)
	return "LuaValue(" .. tostring(node.value) .. ")"
end)

--------------------------------------------------------------------------------
-- Identifier Handler
--------------------------------------------------------------------------------

register_handler("identifier", function(ctx, node, depth)
	local decl_info = ctx:is_declared(node.identifier)
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
	elseif lua_global_libraries[node.identifier] then
		return "get_object(_G->get_item(\"" .. node.identifier .. "\"))"
	elseif node.identifier == "type" then
		return "_G->get_item(\"type\")"
	else
		return sanitize_cpp_identifier(node.identifier)
	end
end)

--------------------------------------------------------------------------------
-- Root and Block Handlers
--------------------------------------------------------------------------------

register_handler("Root", function(ctx, node, depth)
	local cpp_code = ""
	for _, child in ipairs(node.ordered_children) do
		cpp_code = cpp_code .. translate_node(ctx, child, depth + 1) .. "\n"
	end
	return cpp_code
end)

register_handler("block", function(ctx, node, depth)
	local block_code = "{\n"
	local saved_scope = ctx:save_scope()
	
	for _, child in ipairs(node.ordered_children) do
		block_code = block_code .. translate_node(ctx, child, depth + 1)
	end
	
	ctx:restore_scope(saved_scope)
	block_code = block_code .. "}\n"
	return block_code
end)

--------------------------------------------------------------------------------
-- Expression Handlers
--------------------------------------------------------------------------------

register_handler("binary_expression", function(ctx, node, depth)
	local operator = node.value
	
	-- Special handling for logical operators to preserve short-circuiting
	if operator == "and" or operator == "or" then
		local left = translate_node(ctx, node.ordered_children[1], depth + 1)
		
		-- Capture statements for the right-hand side
		ctx:capture_start()
		local right = translate_node(ctx, node.ordered_children[2], depth + 1)
		local right_stmts = ctx:capture_end()
		
		if right_stmts ~= "" then
			-- Wrap right side in a lambda to execute statements lazily
			if operator == "and" then
				return "lua_logical_and(" .. left .. ", [&]() mutable { " .. right_stmts .. "return " .. right .. "; })"
			else
				return "lua_logical_or(" .. left .. ", [&]() mutable { " .. right_stmts .. "return " .. right .. "; })"
			end
		else
			-- No side effects on right side, simple lambda
			if operator == "and" then
				return "lua_logical_and(" .. left .. ", [&]() { return " .. right .. "; })"
			else
				return "lua_logical_or(" .. left .. ", [&]() { return " .. right .. "; })"
			end
		end
	end

	local left = translate_node(ctx, node.ordered_children[1], depth + 1)
	local right = translate_node(ctx, node.ordered_children[2], depth + 1)
	
	-- Arithmetic operators
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
	-- Bitwise operators
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
	-- Comparison operators
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
	-- String concatenation
	elseif operator == ".." then
		return "lua_concat(" .. left .. ", " .. right .. ")"
	else
		return operator .. "(" .. left .. ", " .. right .. ")"
	end
end)

register_handler("unary_expression", function(ctx, node, depth)
	local operator = node.value
	local translated_operand = translate_node(ctx, node.ordered_children[1], depth + 1)
	
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
end)

register_handler("member_expression", function(ctx, node, depth)
	local base_node = node.ordered_children[1]
	local member_node = node.ordered_children[2]
	local base_code = translate_node(ctx, base_node, depth + 1)
	
	if base_node.type == "identifier" and lua_global_libraries[base_node.identifier] then
		-- Use cached access for global library members
		local cache_var = ctx:get_lib_cache(base_node.identifier, member_node.identifier)
		return cache_var
	else
		return "lua_get_member(" .. base_code .. ", LuaValue(\"" .. member_node.identifier .. "\"))"
	end
end)

register_handler("table_index_expression", function(ctx, node, depth)
	local base_node = node.ordered_children[1]
	local index_node = node.ordered_children[2]
	local translated_base = translate_node(ctx, base_node, depth + 1)
	local translated_index = translate_node(ctx, index_node, depth + 1)
	return "lua_get_member(" .. translated_base .. ", " .. translated_index .. ")"
end)

register_handler("expression_list", function(ctx, node, depth)
	local return_values = {}
	for _, expr_node in ipairs(node.ordered_children) do
		table.insert(return_values, translate_node(ctx, expr_node, depth + 1))
	end
	return "{" .. table.concat(return_values, ", ") .. "}"
end)

register_handler("expression_statement", function(ctx, node, depth)
	-- Expression statements (like function calls) are just expressions where we ignore the result
	-- However, since we hoist side effects, we need to flush them.
	local expr_node = node.ordered_children[1]
	local is_call = (expr_node.type == "call_expression" or expr_node.type == "method_call_expression")
	
	-- Pass multiret=true for calls to write directly to buffer, avoiding temp vars
	local code = translate_node(ctx, expr_node, depth + 1, { multiret = is_call })
	local stmts = ctx:flush_statements()
	
	if stmts ~= "" then
		-- If code is just the return buffer name or the monostate literal, don't emit it as a statement
		if code == RET_BUF_NAME or code == "std::monostate{}" then
			return stmts
		else
			return stmts .. code .. ";\n"
		end
	else
		if code == "std::monostate{}" then
			return "" -- Suppress bare std::monostate{};
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
		-- Populate RET_BUF_NAME directly
		ctx:add_statement(RET_BUF_NAME .. ".clear();\n")
		ctx:add_statement("if(n_args > " .. start_index .. ") " .. RET_BUF_NAME .. ".insert(" .. RET_BUF_NAME .. ".end(), args + " .. start_index .. ", args + n_args);\n")
		return RET_BUF_NAME
	else
		-- Return a temporary vector or single value?
		-- Varargs in scalar context returns the first vararg
		local temp_var = "vararg_" .. ctx:get_unique_id()
		ctx:add_statement("LuaValue " .. temp_var .. " = (n_args > " .. start_index .. " ? args[" .. start_index .. "] : LuaValue(std::monostate{}));\n")
		return temp_var
	end
end)

--------------------------------------------------------------------------------
-- Table Constructor Handler
--------------------------------------------------------------------------------

register_handler("table_constructor", function(ctx, node, depth)
	local fields = node:get_all_children_of_type("table_field")
	local temp_table_var = "temp_table_" .. ctx:get_unique_id()
	
	ctx:add_statement("auto " .. temp_table_var .. " = std::make_shared<LuaObject>();\n")
	
	if #fields > 0 then
		local list_index = 1
		for _, field_node in ipairs(fields) do
			local key_child = field_node.ordered_children[1]
			local value_child = field_node.ordered_children[2]
			
			if value_child then
				-- Explicit key-value pair
				local key_part
				if key_child.type == "identifier" then
					key_part = "LuaValue(\"" .. key_child.identifier .. "\")"
				else
					key_part = translate_node(ctx, key_child, depth + 1)
				end
				local value_part = translate_node(ctx, value_child, depth + 1)
				ctx:add_statement(temp_table_var .. "->set_item(" .. key_part .. ", " .. value_part .. ");\n")
			else
				-- Implicit integer key (list-style)
				value_child = key_child
				if value_child.type == "varargs" then
					-- Special handling for varargs in table constructor
					local varargs_buf = translate_node(ctx, value_child, depth + 1, { multiret = true })
					ctx:add_statement("for (size_t i = 0; i < " .. varargs_buf .. ".size(); ++i) {\n")
					ctx:add_statement("  " .. temp_table_var .. "->set_item(LuaValue(static_cast<long long>(" .. list_index .. " + i)), " .. varargs_buf .. "[i]);\n")
					ctx:add_statement("}\n")
					-- We can't easily update list_index here for subsequent fields if mixed, 
					-- but Lua usually puts varargs at the end.
				else
					local key_part = "LuaValue(" .. tostring(list_index) .. "LL)"
					local value_part = translate_node(ctx, value_child, depth + 1)
					ctx:add_statement(temp_table_var .. "->set_item(" .. key_part .. ", " .. value_part .. ");\n")
					list_index = list_index + 1
				end
			end
		end
	end
	
	return temp_table_var
end)

--------------------------------------------------------------------------------
-- Call Expression Handlers
--------------------------------------------------------------------------------

-- Built-in function handlers
local BuiltinCallHandlers = {}

BuiltinCallHandlers["print"] = function(ctx, node, depth, opts)
	local children = node.ordered_children
	local count = #children
	
	for i = 2, count do
		local arg_node = children[i]
		
		if i == count and (is_multiret(arg_node) or is_table_unpack_call(arg_node)) then
			-- Last argument is multiret: expand it
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
		-- print returns nothing (nil), so clear the buffer
		ctx:add_statement(RET_BUF_NAME .. ".clear();\n")
		return RET_BUF_NAME
	else
		return "std::monostate{}"
	end
end

BuiltinCallHandlers["require"] = function(ctx, node, depth, opts)
	local module_name_node = node.ordered_children[2]
	if module_name_node and module_name_node.type == "string" then
		local module_name = module_name_node.value
		ctx:add_required_module(module_name)
		local sanitized_module_name = module_name:gsub("%.", "_")
		
		if opts.multiret then
			ctx:add_statement(RET_BUF_NAME .. " = " .. sanitized_module_name .. "::load();\n")
			return RET_BUF_NAME
		else
			local temp_var = "req_res_" .. ctx:get_unique_id()
			ctx:add_statement("std::vector<LuaValue> " .. temp_var .. "_vec = " .. sanitized_module_name .. "::load();\n")
			ctx:add_statement("LuaValue " .. temp_var .. " = get_return_value(" .. temp_var .. "_vec, 0);\n")
			return temp_var
		end
	end
	return "/* require call with non-string argument */"
end

BuiltinCallHandlers["setmetatable"] = function(ctx, node, depth, opts)
	local table_node = node.ordered_children[2]
	local metatable_node = node.ordered_children[3]
	local translated_table = translate_node(ctx, table_node, depth + 1)
	local translated_metatable = translate_node(ctx, metatable_node, depth + 1)
	
	ctx:add_statement("get_object(" .. translated_table .. ")->set_metatable(get_object(" .. translated_metatable .. "));\n")
	
	if opts.multiret then
		ctx:add_statement(RET_BUF_NAME .. ".clear(); " .. RET_BUF_NAME .. ".push_back(" .. translated_table .. ");\n")
		return RET_BUF_NAME
	else
		return translated_table
	end
end

-- String library method handlers
local StringMethodHandlers = {}

local function handle_string_method(method_name, ctx, node, base_node, depth, opts)
	-- Don't use build_args_code here to avoid duplication of side-effects for manually handled args.
	local base_code = translate_node(ctx, base_node, depth + 1)
	local pattern_code = translate_node(ctx, node.ordered_children[3], depth + 1)
	local replacement_code = (method_name == "gsub") and translate_node(ctx, node.ordered_children[4], depth + 1) or nil
	
	local call_stmt = ""
	if method_name == "match" then
		call_stmt = "lua_string_match("..base_code..", "..pattern_code..", " .. RET_BUF_NAME .. ");\n"
	elseif method_name == "find" then
		call_stmt = "lua_string_find("..base_code..", "..pattern_code..", " .. RET_BUF_NAME .. ");\n"
	elseif method_name == "gsub" then
		call_stmt = "lua_string_gsub("..base_code..", "..pattern_code..", "..replacement_code..", " .. RET_BUF_NAME .. ");\n"
	end
	
	ctx:add_statement(RET_BUF_NAME .. ".clear();\n")
	ctx:add_statement(call_stmt)
	
	if opts.multiret then
		return RET_BUF_NAME
	else
		local temp_var = "str_res_" .. ctx:get_unique_id()
		ctx:add_statement("LuaValue " .. temp_var .. " = get_return_value(" .. RET_BUF_NAME .. ", 0);\n")
		return temp_var
	end
end

StringMethodHandlers["match"] = function(ctx, node, base_node, depth, opts) return handle_string_method("match", ctx, node, base_node, depth, opts) end
StringMethodHandlers["find"] = function(ctx, node, base_node, depth, opts) return handle_string_method("find", ctx, node, base_node, depth, opts) end
StringMethodHandlers["gsub"] = function(ctx, node, base_node, depth, opts) return handle_string_method("gsub", ctx, node, base_node, depth, opts) end

register_handler("call_expression", function(ctx, node, depth, opts)
	local func_node = node.ordered_children[1]
	
	-- Check for string library methods via member expression
	if func_node.type == "member_expression" then
		local base = func_node.ordered_children[1]
		local member = func_node.ordered_children[2]
		if base.identifier == "string" then
			local handler = StringMethodHandlers[member.identifier]
			if handler then
				return handler(ctx, node, node.ordered_children[2], depth, opts)
			end
		end
	end
	
	-- Check for built-in functions
	if func_node.type == "identifier" then
		local handler = BuiltinCallHandlers[func_node.identifier]
		if handler then
			return handler(ctx, node, depth, opts)
		end
	end
	
	-- Generic function call
	-- 1. Build arguments (this hoists arg evaluation statements)
	local args_code, is_vector = build_args_code(ctx, node, 2, depth, nil)
	
	-- 2. Resolve function
	local translated_func_access
	if func_node.type == "identifier" then
		local decl_info = ctx:is_declared(func_node.identifier)
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
		translated_func_access = translate_node(ctx, func_node, depth + 1)
	end
	
	-- 3. Emit Call Statement
	if is_vector then
		-- args_code is the name of a vector variable
		ctx:add_statement("call_lua_value(" .. translated_func_access .. ", " .. args_code .. ".data(), " .. args_code .. ".size(), " .. RET_BUF_NAME .. ");\n")
	else
		if args_code == "" then
			ctx:add_statement("call_lua_value(" .. translated_func_access .. ", {}, 0, " .. RET_BUF_NAME .. ");\n")
		else
			-- Single/Simple args optimization
			ctx:add_statement("call_lua_value(" .. translated_func_access .. ", " .. RET_BUF_NAME .. ", " .. args_code .. ");\n")
		end
	end
	
	-- 4. Return Result
	if opts.multiret then
		return RET_BUF_NAME
	else
		local temp_var = "call_res_" .. ctx:get_unique_id()
		ctx:add_statement("LuaValue " .. temp_var .. " = get_return_value(" .. RET_BUF_NAME .. ", 0);\n")
		return temp_var
	end
end)

-- Method string handlers (for obj:method() syntax)
local MethodCallHandlers = {}

MethodCallHandlers["match"] = function(ctx, node, base_node, depth, opts) return handle_string_method("match", ctx, node, base_node, depth, opts) end
MethodCallHandlers["find"] = function(ctx, node, base_node, depth, opts) return handle_string_method("find", ctx, node, base_node, depth, opts) end
MethodCallHandlers["gsub"] = function(ctx, node, base_node, depth, opts) return handle_string_method("gsub", ctx, node, base_node, depth, opts) end

register_handler("method_call_expression", function(ctx, node, depth, opts)
	local base_node = node.ordered_children[1]
	local method_node = node.ordered_children[2]
	local method_name = method_node.identifier
	local translated_base = translate_node(ctx, base_node, depth + 1)
	
	-- Check for special method handlers
	local handler = MethodCallHandlers[method_name]
	if handler then
		return handler(ctx, node, base_node, depth, opts)
	end
	
	-- Generic method call
	local func_access = "lua_get_member(" .. translated_base .. ", LuaValue(\"" .. method_name .. "\"))"
	
	-- Build args (with self)
	local args_code, is_vector = build_args_code(ctx, node, 3, depth, translated_base)
	
	if is_vector then
		ctx:add_statement("call_lua_value(" .. func_access .. ", " .. args_code .. ".data(), " .. args_code .. ".size(), " .. RET_BUF_NAME .. ");\n")
	else
		ctx:add_statement("call_lua_value(" .. func_access .. ", " .. RET_BUF_NAME .. ", " .. args_code .. ");\n")
	end
	
	if opts.multiret then
		return RET_BUF_NAME
	else
		local temp_var = "mcall_res_" .. ctx:get_unique_id()
		ctx:add_statement("LuaValue " .. temp_var .. " = get_return_value(" .. RET_BUF_NAME .. ", 0);\n")
		return temp_var
	end
end)

--------------------------------------------------------------------------------
-- Declaration and Assignment Handlers
--------------------------------------------------------------------------------

register_handler("local_declaration", function(ctx, node, depth)
	local var_list_node = node.ordered_children[1]
	local expr_list_node = node.ordered_children[2]
	
	local num_vars = #var_list_node.ordered_children
	local num_exprs = expr_list_node and #expr_list_node.ordered_children or 0
	
	local cpp_code = ctx:flush_statements() -- Flush any previous statements
	
	-- Optimization: Single variable, single function call
	if num_vars == 1 and num_exprs == 1 then
		local expr_node = expr_list_node.ordered_children[1]
		if expr_node.type == "call_expression" or expr_node.type == "method_call_expression" then
			local var_node = var_list_node.ordered_children[1]
			local var_name = sanitize_cpp_identifier(var_node.identifier)
			
			-- Translate call with multiret=true to get buffer
			local ret_buf = translate_node(ctx, expr_node, depth + 1, { multiret = true })
			local stmts = ctx:flush_statements()
			
			cpp_code = cpp_code .. stmts
			cpp_code = cpp_code .. "LuaValue " .. var_name .. " = get_return_value(" .. ret_buf .. ", 0);\n"
			ctx:declare_variable(var_name)
			return cpp_code
		end
	end
	
	-- Handle multiple variables/expressions
	local function_call_results_var = nil
	local has_function_call_expr = false
	local first_call_expr_index = -1
	
	-- Check if we need to handle a multi-ret function call
	for i = 1, num_exprs do
		local expr_node = expr_list_node.ordered_children[i]
		if (expr_node.type == "call_expression" or expr_node.type == "method_call_expression") and i == num_exprs then
			has_function_call_expr = true
			first_call_expr_index = i
			break
		end
	end
	
	local init_values = {}
	
	-- Translate expressions
	for i = 1, num_exprs do
		local expr_node = expr_list_node.ordered_children[i]
		if has_function_call_expr and i == first_call_expr_index then
			-- This is the last expression and it's a call, capture the buffer
			local ret_buf = translate_node(ctx, expr_node, depth + 1, { multiret = true })
			local stmts = ctx:flush_statements()
			cpp_code = cpp_code .. stmts
			
			-- Save buffer to a temp vector if needed (though usually we can just use RET_BUF_NAME if we assign immediately)
			-- But to be safe against interleaved calls (unlikely here as it's the last one), we use RET_BUF_NAME.
			function_call_results_var = ret_buf
		else
			local val = translate_node(ctx, expr_node, depth + 1)
			local stmts = ctx:flush_statements()
			cpp_code = cpp_code .. stmts
			init_values[i] = val
		end
	end
	
	-- Declare variables
	for i = 1, num_vars do
		local var_node = var_list_node.ordered_children[i]
		local var_name = sanitize_cpp_identifier(var_node.identifier)
		local initial_value_code = "std::monostate{}"
		
		if i < first_call_expr_index or (not has_function_call_expr and i <= num_exprs) then
			initial_value_code = init_values[i]
		elseif has_function_call_expr and i >= first_call_expr_index then
			local offset = i - first_call_expr_index
			initial_value_code = "get_return_value(" .. function_call_results_var .. ", " .. offset .. ")"
		end
		
		cpp_code = cpp_code .. "LuaValue " .. var_name .. " = " .. initial_value_code .. ";\n"
		ctx:declare_variable(var_name)
	end
	
	return cpp_code
end)

register_handler("assignment", function(ctx, node, depth)
	local var_list_node = node.ordered_children[1]
	local expr_list_node = node.ordered_children[2]
	
	local num_vars = #var_list_node.ordered_children
	local num_exprs = #expr_list_node.ordered_children
	
	local cpp_code = ctx:flush_statements()
	
	-- Similar logic to local_declaration regarding function calls
	local function_call_results_var = nil
	local has_function_call_expr = false
	local first_call_expr_index = -1
	
	for i = 1, num_exprs do
		local expr_node = expr_list_node.ordered_children[i]
		if (expr_node.type == "call_expression" or expr_node.type == "method_call_expression") and i == num_exprs then
			has_function_call_expr = true
			first_call_expr_index = i
			break
		end
	end
	
	local values = {}
	
	for i = 1, num_exprs do
		local expr_node = expr_list_node.ordered_children[i]
		if has_function_call_expr and i == first_call_expr_index then
			local ret_buf = translate_node(ctx, expr_node, depth + 1, { multiret = true })
			local stmts = ctx:flush_statements()
			cpp_code = cpp_code .. stmts
			function_call_results_var = ret_buf
		else
			local val = translate_node(ctx, expr_node, depth + 1)
			local stmts = ctx:flush_statements()
			cpp_code = cpp_code .. stmts
			values[i] = val
		end
	end
	
	for i = 1, num_vars do
		local var_node = var_list_node.ordered_children[i]
		local value_code = "std::monostate{}"
		
		if i < first_call_expr_index or (not has_function_call_expr and i <= num_exprs) then
			value_code = values[i]
		elseif has_function_call_expr and i >= first_call_expr_index then
			local offset = i - first_call_expr_index
			value_code = "get_return_value(" .. function_call_results_var .. ", " .. offset .. ")"
		end
		
		cpp_code = cpp_code .. translate_assignment_target(ctx, var_node, value_code, depth)
	end
	
	return cpp_code
end)

--------------------------------------------------------------------------------
-- Function Declaration Handlers
--------------------------------------------------------------------------------

local function translate_function_body(ctx, node, depth)
	-- Start capturing statements for the function body to isolate them from outer scope
	ctx:capture_start()

	local params_node = node.ordered_children[1]
	local body_node = node.ordered_children[2]
	
	local prev_param_count = ctx.current_function_fixed_params_count
	ctx.current_function_fixed_params_count = 0
	
	-- Save scope
	local saved_scope = ctx:save_scope()
	
	local params_extraction = ""
	local param_index_offset = 0
	
	-- Handle method 'self' parameter
	if node.type == "method_declaration" or node.is_method then
		params_extraction = params_extraction .. "    LuaValue self = (n_args > 0 ? args[0] : LuaValue(std::monostate{}));\n"
		ctx:declare_variable("self")
		param_index_offset = 1
	end
	
	ctx.current_function_fixed_params_count = param_index_offset
	
	-- Extract parameters
	for i, param_node in ipairs(params_node.ordered_children) do
		if param_node.type == "identifier" then
			ctx.current_function_fixed_params_count = ctx.current_function_fixed_params_count + 1
			local param_name = param_node.identifier
			local vector_idx = i + param_index_offset - 1
			params_extraction = params_extraction .. "    LuaValue " .. param_name .. " = (n_args > " .. vector_idx .. " ? args[" .. vector_idx .. "] : LuaValue(std::monostate{}));\n"
			ctx:declare_variable(param_name)
		end
	end
	
	-- Translate body
	local lambda_body = translate_node(ctx, body_node, depth + 1)
	
	-- Check for explicit return
	local has_explicit_return = false
	for _, child_statement in ipairs(body_node.ordered_children) do
		if child_statement.type == "return_statement" then
			has_explicit_return = true
			break
		end
	end
	if not has_explicit_return then
		lambda_body = lambda_body .. "return;\n" -- Void return
	end
	
	-- Capture any remaining statements in the body (should be empty if all statements flushed)
	local body_stmts = ctx:capture_end()

	-- Restore scope
	ctx:restore_scope(saved_scope)
	ctx.current_function_fixed_params_count = prev_param_count
	
	-- Inject reusable return buffer for this function scope
	local buffer_decl = "    std::vector<LuaValue> " .. RET_BUF_NAME .. "; " .. RET_BUF_NAME .. ".reserve(8);\n"
	
	return "std::make_shared<LuaFunctionWrapper>([=](const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result) mutable -> void {\n" .. buffer_decl .. params_extraction .. body_stmts .. lambda_body .. "})"
end

register_handler("function_declaration", function(ctx, node, depth)
	local ptr_name = nil
	
	-- Handle local recursive function with pointer
	if node.is_local and node.identifier then
		ptr_name = node.identifier .. "_ptr_" .. ctx:get_unique_id()
		local sanitized_var_name = sanitize_cpp_identifier(node.identifier)
		ctx:declare_variable(sanitized_var_name, { is_ptr = true, ptr_name = ptr_name })
	end
	
	local lambda_code = translate_function_body(ctx, node, depth)
	
	-- Restore variable declaration for non-pointer access
	if node.is_local and node.identifier then
		local var_name = sanitize_cpp_identifier(node.identifier)
		ctx:declare_variable(var_name)
	end
	
	-- Check for table member function (e.g., function M.greet(name))
	if node.method_name ~= nil then
		local prev_stmts = ctx:flush_statements()
		return prev_stmts .. "get_object(LuaValue(" .. node.identifier .. "))->set(\"" .. node.method_name .. "\", " .. lambda_code .. ");"
	elseif node.identifier ~= nil then
		local prev_stmts = ctx:flush_statements()
		if node.is_local then
			local var_name = sanitize_cpp_identifier(node.identifier)
			return prev_stmts .. "auto " .. ptr_name .. " = std::make_shared<LuaValue>();\n" ..
				"*" .. ptr_name .. " = " .. lambda_code .. ";\n" ..
				"LuaValue " .. var_name .. " = *" .. ptr_name .. ";"
		else
			return prev_stmts .. "_G->set(\"" .. node.identifier .. "\", " .. lambda_code .. ");"
		end
	else
		-- Anonymous function (expression context): DO NOT flush statements.
		-- They will be flushed by the enclosing statement handler.
		return lambda_code
	end
end)

register_handler("method_declaration", function(ctx, node, depth)
	local func_name = node.identifier
	if node.method_name then
		func_name = func_name .. "_" .. node.method_name
	end
	
	local lambda_code = translate_function_body(ctx, node, depth)
	local prev_stmts = ctx:flush_statements()
	
	if node.method_name ~= nil then
		return prev_stmts .. "get_object(LuaValue(" .. node.identifier .. "))->set(\"" .. node.method_name .. "\", " .. lambda_code .. ");"
	else
		return prev_stmts .. lambda_code
	end
end)

--------------------------------------------------------------------------------
-- Control Flow Handlers
--------------------------------------------------------------------------------

register_handler("if_statement", function(ctx, node, depth)
	local cpp_code = ctx:flush_statements()
	local open_count = 0
	
	for i, clause in ipairs(node.ordered_children) do
		if clause.type == "if_clause" then
			ctx:capture_start()
			local cond = translate_node(ctx, clause.ordered_children[1], depth + 1)
			local pre = ctx:capture_end()
			local body = translate_node(ctx, clause.ordered_children[2], depth + 1)
			
			if pre ~= "" then
				cpp_code = cpp_code .. "{\n" .. pre .. "if (is_lua_truthy(" .. cond .. ")) {\n" .. body .. "}"
				open_count = open_count + 1
			else
				cpp_code = cpp_code .. "if (is_lua_truthy(" .. cond .. ")) {\n" .. body .. "}"
			end
		elseif clause.type == "elseif_clause" then
			ctx:capture_start()
			local cond = translate_node(ctx, clause.ordered_children[1], depth + 1)
			local pre = ctx:capture_end()
			local body = translate_node(ctx, clause.ordered_children[2], depth + 1)
			
			if pre ~= "" then
				cpp_code = cpp_code .. " else {\n" .. pre .. "if (is_lua_truthy(" .. cond .. ")) {\n" .. body .. "}"
				open_count = open_count + 1
			else
				cpp_code = cpp_code .. " else if (is_lua_truthy(" .. cond .. ")) {\n" .. body .. "}"
			end
		elseif clause.type == "else_clause" then
			local body = translate_node(ctx, clause.ordered_children[1], depth + 1)
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
	local condition = translate_node(ctx, node.ordered_children[1], depth + 1)
	local pre_stmts = ctx:capture_end()
	
	local body = translate_node(ctx, node.ordered_children[2], depth + 1)
	
	if pre_stmts ~= "" then
		return prev_stmts .. "while (true) {\n" .. pre_stmts .. "if (!is_lua_truthy(" .. condition .. ")) break;\n" .. body .. "}\n"
	else
		return prev_stmts .. "while (is_lua_truthy(" .. condition .. ")) {\n" .. body .. "}\n"
	end
end)

register_handler("repeat_until_statement", function(ctx, node, depth)
	local prev_stmts = ctx:flush_statements()
	
	local block_node = node.ordered_children[1]
	local condition_node = node.ordered_children[2]
	
	local body = translate_node(ctx, block_node, depth + 1)
	
	ctx:capture_start()
	local condition = translate_node(ctx, condition_node, depth + 1)
	local pre_stmts = ctx:capture_end()
	
	if pre_stmts ~= "" then
		return prev_stmts .. "do {\n" .. body .. pre_stmts .. "if (is_lua_truthy(" .. condition .. ")) break;\n} while (true);\n"
	else
		return prev_stmts .. "do {\n" .. body .. "} while (!is_lua_truthy(" .. condition .. "));\n"
	end
end)

register_handler("break_statement", function(ctx, node, depth)
	return ctx:flush_statements() .. "break;\n"
end)

register_handler("label_statement", function(ctx, node, depth)
	return ctx:flush_statements() .. node.value .. ":;\n"
end)

register_handler("goto_statement", function(ctx, node, depth)
	return ctx:flush_statements() .. "goto " .. node.value .. ";\n"
end)

register_handler("return_statement", function(ctx, node, depth)
	local expr_list_node = node.ordered_children[1]
	local cpp_code = ctx:flush_statements()
	
	if expr_list_node and #expr_list_node.ordered_children > 0 then
		for _, expr_node in ipairs(expr_list_node.ordered_children) do
			local val = translate_node(ctx, expr_node, depth + 1)
			local stmts = ctx:flush_statements()
			cpp_code = cpp_code .. stmts .. "out_result.push_back(" .. val .. ");\n"
		end
	end
	return cpp_code .. "return;\n"
end)

--------------------------------------------------------------------------------
-- For Loop Handlers
--------------------------------------------------------------------------------

register_handler("for_numeric_statement", function(ctx, node, depth)
	local prev_stmts = ctx:flush_statements()
	
	local var_name = sanitize_cpp_identifier(node.ordered_children[1].identifier)
	local start_node = node.ordered_children[2]
	local end_node = node.ordered_children[3]
	
	-- Hoist start/end/step calculations to ensure order of evaluation (start -> end -> step)
	-- and to catch any function calls passed as arguments.
	local start_expr_code = translate_node(ctx, start_node, depth + 1)
	local start_stmts = ctx:flush_statements()
	
	local end_expr_code = translate_node(ctx, end_node, depth + 1)
	local end_stmts = ctx:flush_statements()
	
	local step_expr_code
	local body_node
	local step_node = nil
	local step_stmts = ""
	
	if #node.ordered_children == 5 then
		step_node = node.ordered_children[4]
		step_expr_code = translate_node(ctx, step_node, depth + 1)
		step_stmts = ctx:flush_statements()
		body_node = node.ordered_children[5]
	else
		step_expr_code = nil
		body_node = node.ordered_children[4]
	end
	
	ctx:declare_variable(var_name)
	
	-- Optimization for integer loops (literals only)
	local all_integers = (start_node.type == "integer") and (end_node.type == "integer")
	local step_is_one = (step_node == nil) or (step_node.type == "integer" and tonumber(step_node.value) == 1)
	local step_is_neg_one = (step_node and step_node.type == "integer" and tonumber(step_node.value) == -1)
	local step_is_integer = (step_node == nil) or (step_node.type == "integer")
	
	-- Combine hoisted statements (side effects of the expressions)
	local setup_code = prev_stmts .. "{\n" .. start_stmts .. end_stmts .. step_stmts
	
	if all_integers and step_is_one then
		local start_val = tostring(start_node.value)
		local end_val = tostring(end_node.value)
		-- Direct long long iteration, no inner LuaValue casting
		return setup_code .. "for (long long " .. var_name .. " = " .. start_val .. "LL; " .. var_name .. " <= " .. end_val .. "LL; ++" .. var_name .. ") {\n" ..
			translate_node(ctx, body_node, depth + 1) .. "}}\n"

	elseif all_integers and step_is_neg_one then
		local start_val = tostring(start_node.value)
		local end_val = tostring(end_node.value)
		return setup_code .. "for (long long " .. var_name .. " = " .. start_val .. "LL; " .. var_name .. " >= " .. end_val .. "LL; --" .. var_name .. ") {\n" ..
			translate_node(ctx, body_node, depth + 1) .. "}}\n"

	elseif all_integers and step_is_integer then
		local start_val = tostring(start_node.value)
		local end_val = tostring(end_node.value)
		local step_val = tostring(step_node.value)
		local condition = "(" .. step_val .. " >= 0 ? " .. var_name .. " <= " .. end_val .. "LL : " .. var_name .. " >= " .. end_val .. "LL)"
		return setup_code .. "for (long long " .. var_name .. " = " .. start_val .. "LL; " .. condition .. "; " .. var_name .. " += " .. step_val .. "LL) {\n" ..
			translate_node(ctx, body_node, depth + 1) .. "}}\n"

	else
		-- Generic numeric loop (handles variables, doubles, and expression results)
		local limit_var = "limit_" .. ctx:get_unique_id()
		local step_var = "step_" .. ctx:get_unique_id()
		local actual_step = step_expr_code or "1.0"

		-- 1. Evaluate/Fetch Limit and Step as const doubles.
		--    Start is evaluated in the for-loop init.
		local setup_vars = "const double " .. limit_var .. " = get_double(" .. end_expr_code .. ");\n" ..
					       "const double " .. step_var .. " = get_double(" .. actual_step .. ");\n"
		
		-- 2. Determine loop condition based on step direction
		local loop_condition = "(" .. step_var .. " >= 0 ? " .. var_name .. " <= " .. limit_var .. " : " .. var_name .. " >= " .. limit_var .. ")"
		
		-- 3. Construct loop using 'double var_name' directly
		return setup_code .. setup_vars .. 
			   "for (double " .. var_name .. " = get_double(" .. start_expr_code .. "); " .. loop_condition .. "; " .. var_name .. " += " .. step_var .. ") {\n" ..
			   translate_node(ctx, body_node, depth + 1) .. "}}\n"
	end
end)

register_handler("for_generic_statement", function(ctx, node, depth)
	local prev_stmts = ctx:flush_statements()
	
	local var_list_node = node.ordered_children[1]
	local expr_list_node = node.ordered_children[2]
	local body_node = node.ordered_children[3]
	local loop_vars = {}
	
	for _, var_node in ipairs(var_list_node.ordered_children) do
		local sanitized_var = sanitize_cpp_identifier(var_node.identifier)
		table.insert(loop_vars, sanitized_var)
		ctx:declare_variable(sanitized_var)
	end
	
	local iter_func_var = "iter_func_" .. ctx:get_unique_id()
	local iter_state_var = "iter_state_" .. ctx:get_unique_id()
	local iter_value_var = "iter_value_" .. ctx:get_unique_id()
	local results_var = "iter_results_" .. ctx:get_unique_id()
	local current_vals_var = "current_values_" .. ctx:get_unique_id()
	
	local cpp_code = prev_stmts .. "{\n" -- Open block for loop scope
	
	if #expr_list_node.ordered_children == 1 and (expr_list_node.ordered_children[1].type == "call_expression" or expr_list_node.ordered_children[1].type == "method_call_expression") then
		local iterator_call_buf = translate_node(ctx, expr_list_node.ordered_children[1], depth + 1, { multiret = true })
		local stmts = ctx:flush_statements()
		cpp_code = cpp_code .. stmts
		cpp_code = cpp_code .. "std::vector<LuaValue> " .. results_var .. " = " .. iterator_call_buf .. ";\n"
	else
		local iterator_values_code = translate_node(ctx, expr_list_node, depth + 1)
		local stmts = ctx:flush_statements()
		cpp_code = cpp_code .. stmts
		cpp_code = cpp_code .. "std::vector<LuaValue> " .. results_var .. " = " .. iterator_values_code .. ";\n"
	end
	
	cpp_code = cpp_code .. "LuaValue " .. iter_func_var .. " = " .. results_var .. "[0];\n"
	cpp_code = cpp_code .. "LuaValue " .. iter_state_var .. " = " .. results_var .. "[1];\n"
	cpp_code = cpp_code .. "LuaValue " .. iter_value_var .. " = " .. results_var .. "[2];\n"
	
	-- Create reusable buffer outside loop
	cpp_code = cpp_code .. "std::vector<LuaValue> " .. current_vals_var .. ";\n"
	cpp_code = cpp_code .. current_vals_var .. ".reserve(3);\n"
	
	cpp_code = cpp_code .. "while (true) {\n"
	cpp_code = cpp_code .. "    std::vector<LuaValue> args_obj = {" .. iter_state_var .. ", " .. iter_value_var .. "};\n"
	
	-- Direct call to void function with output parameter
	cpp_code = cpp_code .. "    call_lua_value(" .. iter_func_var .. ", args_obj.data(), args_obj.size(), " .. current_vals_var .. ");\n"
	
	cpp_code = cpp_code .. "    if (" .. current_vals_var .. ".empty() || std::holds_alternative<std::monostate>(" .. current_vals_var .. "[0])) {\n"
	cpp_code = cpp_code .. "        break;\n"
	cpp_code = cpp_code .. "    }\n"
	
	for i, var_name in ipairs(loop_vars) do
		cpp_code = cpp_code .. "    LuaValue " .. var_name .. " = (" .. current_vals_var .. ".size() > " .. (i - 1) .. ") ? " .. current_vals_var .. "[" .. (i - 1) .. "] : std::monostate{};\n"
	end
	
	cpp_code = cpp_code .. "    " .. iter_value_var .. " = " .. loop_vars[1] .. ";\n"
	cpp_code = cpp_code .. translate_node(ctx, body_node, depth + 1) .. "\n"
	cpp_code = cpp_code .. "}}\n"
	
	return cpp_code
end)

--------------------------------------------------------------------------------
-- CppTranslator Class Methods
--------------------------------------------------------------------------------

-- Helper function to emit cache declarations for library member accesses
local function emit_cache_declarations(ctx)
	local code = ""
	for cache_key, cache_var in pairs(ctx.lib_member_caches) do
		-- Parse cache_key back to lib.member (format: "lib_member")
		local lib, member = cache_key:match("^([^_]+)_(.+)$")
		if lib and member then
			code = code .. "static const LuaValue " .. cache_var 
				.. " = get_object(_G->get_item(\"" .. lib .. "\"))->get_item(\"" .. member .. "\");\n"
		end
	end
	return code
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
	-- Create translation context
	local ctx = TranslatorContext:new(self, for_header, current_module_object_name, is_main_script)
	
	-- Translate the AST
	local generated_code = translate_node(ctx, ast_root, 0)
	
	-- Flush any remaining top-level statements
	local remaining_stmts = ctx:flush_statements()
	generated_code = remaining_stmts .. generated_code
	
	-- Build the header
	local header = "#include <iostream>\n#include <vector>\n#include <string>\n#include <map>\n#include <memory>\n#include <variant>\n#include <functional>\n#include <cmath>\n#include \"lua_object.hpp\"\n\n"
	
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
			-- Emit cache declarations for library member accesses
			local cache_decls = emit_cache_declarations(ctx)
			local buffer_decl = "    std::vector<LuaValue> " .. RET_BUF_NAME .. "; " .. RET_BUF_NAME .. ".reserve(10);\n"
			local main_function_start = "int main(int argc, char* argv[]) {\n" ..
										"init_G(argc, argv);\n" .. cache_decls .. buffer_decl
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
			for var_name, _ in pairs(ctx.module_global_vars) do
				global_var_extern_declarations = global_var_extern_declarations .. "extern LuaValue " .. var_name .. ";\n"
			end
			return hpp_header .. global_var_extern_declarations .. namespace_start .. hpp_load_function_declaration .. namespace_end
		else
			local cpp_header = "#include \"" .. file_name .. ".hpp\"\n"
			local global_var_definitions = ""
			for var_name, _ in pairs(ctx.module_global_vars) do
				global_var_definitions = global_var_definitions .. "LuaValue " .. var_name .. ";\n"
			end
			local module_body_code = ""
			local module_identifier = ""
			local explicit_return_found = false
			local explicit_return_value = "std::make_shared<LuaObject>()"
			
			for _, child in ipairs(ast_root.ordered_children) do
				if child.type == "local_declaration" and #child.ordered_children >= 2 and child.ordered_children[1].type == "variable" and child.ordered_children[2].type == "table_constructor" then
					module_identifier = child.ordered_children[1].identifier
					module_body_code = module_body_code .. translate_node(ctx, child, 0) .. "\n"
				elseif child.type == "return_statement" then
					explicit_return_found = true
					local return_expr_node = child.ordered_children[1]
					if return_expr_node then
						if return_expr_node.type == "expression_list" then
							local return_values = {}
							for _, expr_node in ipairs(return_expr_node.ordered_children) do
								table.insert(return_values, translate_node(ctx, expr_node, 0))
							end
							explicit_return_value = table.concat(return_values, ", ")
						else
							explicit_return_value = translate_node(ctx, return_expr_node, 0)
						end
					else
						explicit_return_value = "std::monostate{}"
					end
					-- Flush statements for return
					module_body_code = module_body_code .. ctx:flush_statements()
				else
					module_body_code = module_body_code .. translate_node(ctx, child, 0) .. "\n"
				end
			end
			
			local load_function_body = module_body_code
			
			if explicit_return_found then
				load_function_body = load_function_body .. "return {" .. explicit_return_value .. "};\n"
			elseif module_identifier ~= "" then
				load_function_body = load_function_body .. "return {" .. module_identifier .. "};\n"
			else
				load_function_body = load_function_body .. "return {std::make_shared<LuaObject>()};\n"
			end
			
			-- Emit cache declarations for library member accesses at start of load function
			local cache_decls = emit_cache_declarations(ctx)
			local buffer_decl = "    std::vector<LuaValue> " .. RET_BUF_NAME .. "; " .. RET_BUF_NAME .. ".reserve(10);\n"
			local load_function_definition = "std::vector<LuaValue> load() {\n" .. cache_decls .. buffer_decl .. load_function_body .. "}\n"
			return header .. cpp_header .. global_var_definitions .. namespace_start .. load_function_definition .. namespace_end
		end
	end
end

return CppTranslator