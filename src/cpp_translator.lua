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
	ctx.global_identifier_caches = {}
	ctx.current_return_stmt = is_main_script and "return 0;" or "return out_result;"
	
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
		self.lib_member_caches[cache_key] = "_cache_lib_" .. cache_key
		-- Also ensure the library itself is cached as a global
		if not self.global_identifier_caches[lib_name] then
			self.global_identifier_caches[lib_name] = "_cache_global_" .. lib_name
		end
	end
	return self.lib_member_caches[cache_key]
end

function TranslatorContext:get_string_cache(s)
	if not self.string_literals[s] then
		-- Generate a safe variable name for the string
		local safe_s = s:gsub("[^%a%d]", "_")
		if #safe_s > 20 then safe_s = safe_s:sub(1, 20) end
		local id = self:get_unique_id()
		self.string_literals[s] = "_cache_str_" .. safe_s .. "_" .. id
	end
	return self.string_literals[s]
end

function TranslatorContext:get_global_cache(name)
	if not self.global_identifier_caches[name] then
		self.global_identifier_caches[name] = "_cache_global_" .. name
	end
	return self.global_identifier_caches[name]
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
		print("ERROR: Recursion limit exceeded in translate_node. Node type: " .. (node and node[1] or "nil"))
		os.exit(1)
	end
	
	if not node then
		return ""
	end
	
	if not node[1] then
		print("ERROR: Node has no type field:", node)
		return "/* ERROR: Node with no type */"
	end
	
	local handler = NodeHandlers[node[1]]
	if handler then
		return handler(ctx, node, depth, opts)
	else
		print("WARNING: Unhandled node type: " .. node[1])
		return "/* UNHANDLED_NODE_TYPE: " .. node[1] .. " */"
	end
end

--------------------------------------------------------------------------------
-- Helper: Check if node is a table.unpack call
--------------------------------------------------------------------------------

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

--------------------------------------------------------------------------------
-- Helper: Check if node returns multiple values (call, varargs matches)
--------------------------------------------------------------------------------

local function is_multiret(node)
	return node[1] == "call_expression" or 
		   node[1] == "method_call_expression" or
		   node[1] == "varargs"
end

--------------------------------------------------------------------------------
-- Helper: Build Arguments Code
-- Returns: code_string, is_vector_boolean
--------------------------------------------------------------------------------

local function build_args_code(ctx, node, start_index, depth, self_arg)
	local children = node[5]
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
		ctx:add_statement("LuaValueVector " .. vec_var .. "; ")
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
		local var_code
		local declaration_prefix = ""
		if var_node[1] == "identifier" and not ctx:is_declared(var_node[3]) then
			ctx:declare_variable(var_node[3])
			if ctx.is_main_script then
				declaration_prefix = "LuaValue "
			else
				ctx:add_module_global_var(var_node[3])
			end
			var_code = sanitize_cpp_identifier(var_node[3])
		else
			var_code = translate_node(ctx, var_node, depth + 1)
		end
		return declaration_prefix .. var_code .. " = " .. value_code .. ";\n"
	end
end

--------------------------------------------------------------------------------
-- Literal Handlers
--------------------------------------------------------------------------------

register_handler("string", function(ctx, node, depth)
	local s = node[2]
	return ctx:get_string_cache(s)
end)

register_handler("number", function(ctx, node, depth)
	local num_str = tostring(node[2])
	if not string.find(num_str, "%.") then
		return num_str .. "LL"
	else
		return num_str
	end
end)

register_handler("integer", function(ctx, node, depth)
	local val = tonumber(node[2])
	if val and val >= 0 and val <= 10 then
		return ctx:get_global_cache("const_int_" .. val)
	end
	return tostring(node[2]) .. "LL"
end)

register_handler("boolean", function(ctx, node, depth)
	return "LuaValue(" .. tostring(node[2]) .. ")"
end)

--------------------------------------------------------------------------------
-- Identifier Handler
--------------------------------------------------------------------------------

register_handler("identifier", function(ctx, node, depth)
	local decl_info = ctx:is_declared(node[3])
	if decl_info then
		if type(decl_info) == "table" and decl_info.is_ptr then
			return "(*" .. decl_info.ptr_name .. ")"
		end
		return sanitize_cpp_identifier(node[3])
	end
	
	if node[3] == "nil" then
		return "std::monostate{}"
	elseif lua_global_libraries[node[3]] then
		-- Cache standard library objects
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
	local cpp_code = ""
	for _, child in ipairs(node[5] or empty_table) do
		cpp_code = cpp_code .. translate_node(ctx, child, depth + 1) .. "\n"
	end
	return cpp_code
end)

register_handler("block", function(ctx, node, depth, opts)
	local block_code = (opts and opts.no_braces) and "" or "{\n"
	local saved_scope = ctx:save_scope()
	
	for _, child in ipairs(node[5] or empty_table) do
		block_code = block_code .. translate_node(ctx, child, depth + 1)
	end
	
	ctx:restore_scope(saved_scope)
	block_code = block_code .. ((opts and opts.no_braces) and "" or "}\n")
	return block_code
end)

--------------------------------------------------------------------------------
-- Expression Handlers
--------------------------------------------------------------------------------

register_handler("binary_expression", function(ctx, node, depth)
	local operator = node[2]
	
	-- Special handling for logical operators to preserve short-circuiting
	if (operator == "and" or operator == "or") then
		local left = translate_node(ctx, node[5][1], depth + 1)
		local left_stmts = ctx:flush_statements()
		local temp_var = "logic_res_" .. ctx:get_unique_id()
		
		ctx:add_statement(left_stmts)
		ctx:add_statement("LuaValue " .. temp_var .. " = " .. left .. ";\n")
		
		if operator == "and" then
			ctx:add_statement("if (is_lua_truthy(" .. temp_var .. ")) {\n")
		else -- or
			ctx:add_statement("if (!is_lua_truthy(" .. temp_var .. ")) {\n")
		end
		
		ctx:capture_start()
		local right = translate_node(ctx, node[5][2], depth + 1)
		local right_stmts = ctx:capture_end()
		ctx:add_statement(right_stmts)
		ctx:add_statement("    " .. temp_var .. " = " .. right .. ";\n")
		ctx:add_statement("}\n")
		
		return temp_var
	end

	local left = translate_node(ctx, node[5][1], depth + 1)
	local right = translate_node(ctx, node[5][2], depth + 1)
	
	-- Arithmetic operators
	if operator == "+" then
		return "(" .. left .. " + " .. right .. ")"
	elseif operator == "-" then
		return "(" .. left .. " - " .. right .. ")"
	elseif operator == "*" then
		return "(" .. left .. " * " .. right .. ")"
	elseif operator == "/" then
		return "(" .. left .. " / " .. right .. ")"
	elseif operator == "//" then
		return "LuaValue(static_cast<long long>(std::floor(get_double(" .. left .. " / " .. right .. "))))"
	elseif operator == "%" then
		return "(" .. left .. " % " .. right .. ")"
	elseif operator == "^" then
		return "pow(get_double(" .. left .. "), get_double(" .. right .. "))"
	-- Bitwise operators
	elseif operator == "&" then
		return "(" .. left .. " & " .. right .. ")"
	elseif operator == "|" then
		return "(" .. left .. " | " .. right .. ")"
	elseif operator == "~" then
		return "(" .. left .. " ^ " .. right .. ")"
	elseif operator == "<<" then
		return "(" .. left .. " << " .. right .. ")"
	elseif operator == ">>" then
		return "(" .. left .. " >> " .. right .. ")"
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
	local operator = node[2]
	local translated_operand = translate_node(ctx, node[5][1], depth + 1)
	
	if operator == "-" then
		return "(-" .. translated_operand .. ")"
	elseif operator == "not" then
		return "(!is_lua_truthy(" .. translated_operand .. "))"
	elseif operator == "#" then
		return "lua_get_length(" .. translated_operand .. ")"
	elseif operator == "~" then
		return "(~" .. translated_operand .. ")"
	else
		return operator .. " " .. translated_operand
	end
end)

register_handler("member_expression", function(ctx, node, depth)
	local base_node = node[5][1]
	local member_node = node[5][2]
	local base_code = translate_node(ctx, base_node, depth + 1)
	
	if base_node[1] == "identifier" and lua_global_libraries[base_node[3]] then
		-- Use cached access for global library members
		local cache_var = ctx:get_lib_cache(base_node[3], member_node[3])
		return cache_var
	else
		-- Optimization: cache member name string literal
		local member_cache_var = ctx:get_string_cache(member_node[3])
		return "lua_get_member(" .. base_code .. ", " .. member_cache_var .. ")"
	end
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
	-- Expression statements (like function calls) are just expressions where we ignore the result
	-- However, since we hoist side effects, we need to flush them.
	local expr_node = node[5][1]
	local is_call = (expr_node[1] == "call_expression" or expr_node[1] == "method_call_expression")
	
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
			-- Explicit key-value pair
			local key_part
			if key_child[1] == "identifier" then
				key_part = "{" .. ctx:get_string_cache(key_child[3]) .. ", "
			else
				key_part = "{" .. translate_node(ctx, key_child, depth + 1) .. ", "
			end
			local value_part = translate_node(ctx, value_child, depth + 1)
			table.insert(props, key_part .. value_part .. "}")
		else
			-- Implicit integer key (list-style)
			value_child = key_child
			if value_child[1] == "varargs" then
				has_complex = true
				-- We will handle varargs after initial construction
				table.insert(complex_statements, { "varargs", value_child, list_index })
			else
				local value_part = translate_node(ctx, value_child, depth + 1)
				table.insert(array, value_part)
				list_index = list_index + 1
			end
		end
	end

	if #props > 0 and #props <= 4 and #array == 0 and not has_complex then
		ctx:add_statement("auto " .. temp_table_var .. " = LuaObject::create(" .. table.concat(props, ", ") .. ");\n")
	else
		local props_str = "{" .. table.concat(props, ", ") .. "}"
		local array_str = "{" .. table.concat(array, ", ") .. "}"
		ctx:add_statement("auto " .. temp_table_var .. " = LuaObject::create(" .. props_str .. ", " .. array_str .. ");\n")
	end

	if has_complex then
		for _, stmt in ipairs(complex_statements) do
			if stmt[1] == "varargs" then
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

-- Built-in function handlers
local BuiltinCallHandlers = {}

BuiltinCallHandlers["print"] = function(ctx, node, depth, opts)
	local children = node[5]
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

BuiltinCallHandlers["table.insert"] = function(ctx, node, depth, opts)
	local children = node[5]
	local count = #children -- Includes the function node at index 1
	if count == 3 then -- table.insert(t, v)
		local table_code = translate_node(ctx, children[2], depth + 1)
		local value_code = translate_node(ctx, children[3], depth + 1)
		ctx:add_statement("lua_table_insert(" .. table_code .. ", " .. value_code .. ");\n")
		return "std::monostate{}"
	elseif count == 4 then -- table.insert(t, pos, v)
		local table_code = translate_node(ctx, children[2], depth + 1)
		local pos_code = translate_node(ctx, children[3], depth + 1)
		local value_code = translate_node(ctx, children[4], depth + 1)
        ctx:add_statement("lua_table_insert(" .. table_code .. ", get_long_long(" .. pos_code .. "), " .. value_code .. ");\n")
		return "std::monostate{}"
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
			ctx:add_statement(RET_BUF_NAME .. " = " .. sanitized_module_name .. "::load();\n")
			return RET_BUF_NAME
		else
			local temp_var = "req_res_" .. ctx:get_unique_id()
			ctx:add_statement("LuaValueVector " .. temp_var .. "_vec = " .. sanitized_module_name .. "::load();\n")
			ctx:add_statement("LuaValue " .. temp_var .. " = get_return_value(" .. temp_var .. "_vec, 0);\n")
			return temp_var
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
	local pattern_code = translate_node(ctx, node[5][3], depth + 1)
	local replacement_code = (method_name == "gsub") and translate_node(ctx, node[5][4], depth + 1) or nil
	
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
	local func_node = node[5][1]
	
	-- Check for string library methods via member expression
	if func_node[1] == "member_expression" then
		local base = func_node[5][1]
		local member = func_node[5][2]
		if base[3] == "string" then
			local handler = StringMethodHandlers[member[3]]
			if handler then
				return handler(ctx, node, node[5][2], depth, opts)
			end
		end
		if base[3] == "table" then
			local handler = BuiltinCallHandlers["table." .. member[3]]
			if handler then
				return handler(ctx, node, depth, opts)
			end
		end
	end
	
	-- Check for built-in functions
	if func_node[1] == "identifier" then
		local handler = BuiltinCallHandlers[func_node[3]]
		if handler then
			return handler(ctx, node, depth, opts)
		end
	end
	
	-- Check for inlinable local functions (character predicates like is_digit, is_alpha)
	-- These are compiled to direct C++ inline calls for performance
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
			-- This is a local function with a known inline equivalent
			local arg_node = node[5][2]
			if arg_node then
				local arg_code = translate_node(ctx, arg_node, depth + 1)
				-- Return the inline C++ call directly (returns bool)
				if opts.multiret then
					ctx:add_statement(RET_BUF_NAME .. ".clear(); " .. RET_BUF_NAME .. ".push_back(LuaValue(" .. inline_func .. "(" .. arg_code .. ")));\\n")
					return RET_BUF_NAME
				else
					return inline_func .. "(" .. arg_code .. ")"
				end
			end
		end
	end
	
	-- Generic function call
	-- 1. Build arguments (this hoists arg evaluation statements)
	local args_code, is_vector = build_args_code(ctx, node, 2, depth, nil)
	
	-- 2. Resolve function
	local translated_func_access
	if func_node[1] == "identifier" then
		local decl_info = ctx:is_declared(func_node[3])
		if decl_info then
			if type(decl_info) == "table" and decl_info.is_ptr then
				translated_func_access = "(*" .. decl_info.ptr_name .. ")"
			else
				translated_func_access = sanitize_cpp_identifier(func_node[3])
			end
		elseif func_node[3] == "insert" and ctx:is_declared("table") then
            -- Check if it's table.insert(t, v) or t:insert(v)
            -- This is tricky because we don't always know if 'table' is the global lib
            -- But if it's not declared as a local, it's likely the global.
            -- However, the handler above handles table.insert via BuiltinCallHandlers if it's a member expression.
		    local func_cache_var = ctx:get_string_cache(func_node[3])
			translated_func_access = "_G->get(" .. func_cache_var .. ")"
		else
			local func_cache_var = ctx:get_string_cache(func_node[3])
			translated_func_access = "_G->get(" .. func_cache_var .. ")"
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

StringMethodHandlers["byte"] = function(ctx, node, base_node, depth, opts) return MethodCallHandlers["byte"](ctx, node, base_node, depth, opts) end
StringMethodHandlers["sub"] = function(ctx, node, base_node, depth, opts) return MethodCallHandlers["sub"](ctx, node, base_node, depth, opts) end

MethodCallHandlers["byte"] = function(ctx, node, base_node, depth, opts)
	-- Check for str:byte(i) or str:byte(i, i) pattern -> lua_string_byte_at(str, i)
	local arg1 = node[5][3]
	local arg2 = node[5][4]
	local arg3 = node[5][5]
	
	if arg1 and not arg2 then
		-- str:byte(i) single char
		local arg1_code = translate_node(ctx, arg1, depth + 1)
		local base_code = translate_node(ctx, base_node, depth + 1)
		
		if opts.multiret then
			ctx:add_statement("lua_string_byte(" .. base_code .. ", " .. arg1_code .. ", " .. arg1_code .. ", " .. RET_BUF_NAME .. ");\n")
			return RET_BUF_NAME
		else
			return "lua_string_byte_at_raw(" .. base_code .. ", " .. arg1_code .. ")"
		end
	elseif arg1 and arg2 and not arg3 then
		-- str:byte(i, j)
		local arg1_code = translate_node(ctx, arg1, depth + 1)
		local arg2_code = translate_node(ctx, arg2, depth + 1)
		
		if arg1_code == arg2_code then
			local base_code = translate_node(ctx, base_node, depth + 1)
			if opts.multiret then
				ctx:add_statement("lua_string_byte(" .. base_code .. ", " .. arg1_code .. ", " .. arg1_code .. ", " .. RET_BUF_NAME .. ");\n")
				return RET_BUF_NAME
			else
				return "lua_string_byte_at_raw(" .. base_code .. ", " .. arg1_code .. ")"
			end
		end
	end
    
    -- Fallback to the general library call if possible
    local base_code = translate_node(ctx, base_node, depth + 1)
    local args_code, is_vector = build_args_code(ctx, node, 3, depth, base_code)
    -- We can't easily call the library method here without risking the same overhead we're trying to avoid.
    -- But since this is a method call, it will normally fall through to the generic method call handler below.
	return nil
end

MethodCallHandlers["insert"] = function(ctx, node, base_node, depth, opts)
    local arg1 = node[5][3]
    local arg2 = node[5][4]
    local arg3 = node[5][5]
    
    if arg1 and not arg2 then
        -- t:insert(v)
        local base_code = translate_node(ctx, base_node, depth + 1)
        local value_code = translate_node(ctx, arg1, depth + 1)
        ctx:add_statement("lua_table_insert(" .. base_code .. ", " .. value_code .. ");\n")
        return "std::monostate{}"
    end
    return nil
end

MethodCallHandlers["sub"] = function(ctx, node, base_node, depth, opts)
	-- Check for str:sub(i, i) pattern -> lua_string_char_at(str, i)
	local arg1 = node[5][3]
	local arg2 = node[5][4]
	local arg3 = node[5][5]
	
	if arg1 and arg2 and not arg3 then
		-- We optimize if we can prove args are identical, or just simple vars
		-- For now, let's translate them and compare the generated code string
		local arg1_code = translate_node(ctx, arg1, depth + 1)
		local arg2_code = translate_node(ctx, arg2, depth + 1)
		
		if arg1_code == arg2_code then
			local base_code = translate_node(ctx, base_node, depth + 1)
			if opts.multiret then
				ctx:add_statement(RET_BUF_NAME .. ".clear(); " .. RET_BUF_NAME .. ".push_back(lua_string_char_at(" .. base_code .. ", " .. arg1_code .. "));\n")
				return RET_BUF_NAME
			else
				return "lua_string_char_at(" .. base_code .. ", " .. arg1_code .. ")"
			end
		end
        
        -- Optimization for general sub(i, j)
        local base_code = translate_node(ctx, base_node, depth + 1)
        if not opts.multiret then
            return "lua_string_sub(" .. base_code .. ", get_long_long(" .. arg1_code .. "), get_long_long(" .. arg2_code .. "))"
        end
	end
	return nil
end

register_handler("method_call_expression", function(ctx, node, depth, opts)
	local base_node = node[5][1]
	local method_node = node[5][2]
	local method_name = method_node[3]
	local translated_base = translate_node(ctx, base_node, depth + 1)
	
	-- Check for special method handlers
	local handler = MethodCallHandlers[method_name]
	if handler then
		local res = handler(ctx, node, base_node, depth, opts)
		if res then return res end
	end
	
	-- Generic method call
	-- 1. Translate base object (caching statements)
	local base_code = translate_node(ctx, base_node, depth + 1)
	local method_cache_var = ctx:get_string_cache(method_name)
	
	-- 2. Build arguments (this hoists arg evaluation statements)
	local args_code, is_vector = build_args_code(ctx, node, 3, depth, base_code)
	
	-- 3. Emit Call Statement
	if is_vector then
		-- args_code is the name of a vector variable
		ctx:add_statement("call_lua_value(lua_get_member(" .. base_code .. ", " .. method_cache_var .. "), " .. args_code .. ".data(), " .. args_code .. ".size(), " .. RET_BUF_NAME .. ");\n")
	else
		-- Single/Simple args optimization
		ctx:add_statement("call_lua_value(lua_get_member(" .. base_code .. ", " .. method_cache_var .. "), " .. RET_BUF_NAME .. ", " .. args_code .. ");\n")
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
	local var_list_node = node[5][1]
	local expr_list_node = node[5][2]
	
	local num_vars = #(var_list_node[5] or empty_table)
	local num_exprs = expr_list_node and #(expr_list_node[5] or empty_table) or 0
	
	local cpp_code = ctx:flush_statements() -- Flush any previous statements
	
	-- Optimization: Single variable, single function call
	if num_vars == 1 and num_exprs == 1 then
		local expr_node = expr_list_node[5][1]
		if expr_node[1] == "call_expression" or expr_node[1] == "method_call_expression" then
			local var_node = var_list_node[5][1]
			local var_name = sanitize_cpp_identifier(var_node[3])
			
			-- Translate call with multiret=false
			local val = translate_node(ctx, expr_node, depth + 1, { multiret = false })
			local stmts = ctx:flush_statements()
			
			cpp_code = cpp_code .. stmts
			cpp_code = cpp_code .. "LuaValue " .. var_name .. " = " .. val .. ";\n"
			ctx:declare_variable(var_node[3])
			return cpp_code
		end
	end
	
	-- Handle multiple variables/expressions
	local function_call_results_var = nil
	local has_function_call_expr = false
	local first_call_expr_index = -1
	
	-- Check if we need to handle a multi-ret function call
	for i = 1, num_exprs do
		local expr_node = expr_list_node[5][i]
		if (expr_node[1] == "call_expression" or expr_node[1] == "method_call_expression") and i == num_exprs and num_vars > i then
			has_function_call_expr = true
			first_call_expr_index = i
			break
		end
	end
	
	local init_values = {}
	
	-- Translate expressions
	for i = 1, num_exprs do
		local expr_node = expr_list_node[5][i]
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
		local var_node = var_list_node[5][i]
		local var_name = sanitize_cpp_identifier(var_node[3])
		local initial_value_code = "std::monostate{}"
		
		if i < first_call_expr_index or (not has_function_call_expr and i <= num_exprs) then
			initial_value_code = init_values[i]
		elseif has_function_call_expr and i >= first_call_expr_index then
			local offset = i - first_call_expr_index
			initial_value_code = "get_return_value(" .. function_call_results_var .. ", " .. offset .. ")"
		end
		
		cpp_code = cpp_code .. "LuaValue " .. var_name .. " = " .. initial_value_code .. ";\n"
		ctx:declare_variable(var_node[3])
	end
	
	return cpp_code
end)

register_handler("assignment", function(ctx, node, depth)
	local var_list_node = node[5][1]
	local expr_list_node = node[5][2]
	
	local num_vars = #(var_list_node[5] or empty_table)
	local num_exprs = #(expr_list_node[5] or empty_table)
	
	local cpp_code = ctx:flush_statements()
	
	-- Similar logic to local_declaration regarding function calls
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
	
	for i = 1, num_exprs do
		local expr_node = expr_list_node[5][i]
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
		local var_node = var_list_node[5][i]
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

	local params_node = node[5][1]
	local body_node = node[5][2]
	
	local prev_param_count = ctx.current_function_fixed_params_count
	ctx.current_function_fixed_params_count = 0
	
	-- Save scope
	local saved_scope = ctx:save_scope()
	
	local params_extraction = ""
	local param_index_offset = 0
	
	-- Handle method 'self' parameter
	if node[1] == "method_declaration" or node.is_method then
		params_extraction = params_extraction .. "    LuaValue self = (n_args > 0 ? args[0] : LuaValue(std::monostate{}));\n"
		ctx:declare_variable("self")
		param_index_offset = 1
	end
	
	ctx.current_function_fixed_params_count = param_index_offset
	
	-- Extract parameters
	for i, param_node in ipairs(params_node[5] or empty_table) do
		if param_node[1] == "identifier" then
			ctx.current_function_fixed_params_count = ctx.current_function_fixed_params_count + 1
			local param_name = param_node[3]
			local vector_idx = i + param_index_offset - 1
			params_extraction = params_extraction .. "    LuaValue " .. param_name .. " = (n_args > " .. vector_idx .. " ? args[" .. vector_idx .. "] : LuaValue(std::monostate{}));\n"
			ctx:declare_variable(param_name)
		end
	end
	
	-- Inject reusable return buffer for this function scope
	local buffer_decl = "    LuaValueVector " .. RET_BUF_NAME .. "; " .. RET_BUF_NAME .. ".reserve(8);\n"
	
	local saved_return_stmt = ctx.current_return_stmt
	ctx.current_return_stmt = "return;"
	
	local body_code = translate_node(ctx, body_node, depth + 1, { no_braces = true })
	
	ctx.current_return_stmt = saved_return_stmt
	
	-- Capture any remaining statements in the body (should be empty if all statements flushed)
	local body_stmts = ctx:capture_end()

	-- Restore scope
	ctx:restore_scope(saved_scope)
	ctx.current_function_fixed_params_count = prev_param_count
	
	return "[=](const LuaValue* args, size_t n_args, LuaValueVector& out_result) mutable -> void {\n" .. buffer_decl .. params_extraction .. body_code .. body_stmts .. "}"
end

register_handler("function_expression", function(ctx, node, depth)
	return "std::make_shared<LuaFunctionWrapper>(" .. translate_function_body(ctx, node, depth) .. ")"
end)

register_handler("function_declaration", function(ctx, node, depth)
	local ptr_name = nil
	
	-- Handle local recursive function with pointer
	if node[6] and node[3] then
		ptr_name = node[3] .. "_ptr_" .. ctx:get_unique_id()
		local sanitized_var_name = sanitize_cpp_identifier(node[3])
		ctx:declare_variable(sanitized_var_name, { is_ptr = true, ptr_name = ptr_name })
	end
	
	local lambda_code = translate_function_body(ctx, node, depth)
	
	-- Restore variable declaration for non-pointer access
	if node[6] and node[3] then
		local var_name = sanitize_cpp_identifier(node[3])
		ctx:declare_variable(node[3])
	end
	
	-- Check for table member function (e.g., function M.greet(name))
	if node[7] ~= nil then
		local prev_stmts = ctx:flush_statements()
		return prev_stmts .. "get_object(" .. sanitize_cpp_identifier(node[3]) .. ")->set(\"" .. node[7] .. "\", std::make_shared<LuaFunctionWrapper>(" .. lambda_code .. "));"
	elseif node[3] ~= nil then
		local prev_stmts = ctx:flush_statements()
		if node[6] then
			local var_name = sanitize_cpp_identifier(node[3])
			return prev_stmts .. "auto " .. ptr_name .. " = std::make_shared<LuaValue>();\n" ..
				"*" .. ptr_name .. " = std::make_shared<LuaFunctionWrapper>(" .. lambda_code .. ");\n" ..
				"LuaValue " .. var_name .. " = *" .. ptr_name .. ";"
		else
			return prev_stmts .. "_G->set(\"" .. node[3] .. "\", std::make_shared<LuaFunctionWrapper>(" .. lambda_code .. "));"
		end
	else
		-- Anonymous function (expression context)
		return "std::make_shared<LuaFunctionWrapper>(" .. lambda_code .. ")"
	end
end)

register_handler("method_declaration", function(ctx, node, depth)
	local lambda_code = translate_function_body(ctx, node, depth)
	local prev_stmts = ctx:flush_statements()
	
	if node[7] ~= nil then
		return prev_stmts .. "get_object(" .. sanitize_cpp_identifier(node[3]) .. ")->set(\"" .. node[7] .. "\", std::make_shared<LuaFunctionWrapper>(" .. lambda_code .. "));"
	else
		return prev_stmts .. "std::make_shared<LuaFunctionWrapper>(" .. lambda_code .. ")"
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
			local cond = translate_node(ctx, clause[5][1], depth + 1)
			local pre = ctx:capture_end()
			local body = translate_node(ctx, clause[5][2], depth + 1, { no_braces = true })
			
			if pre ~= "" then
				cpp_code = cpp_code .. "{\n" .. pre .. "if (is_lua_truthy(" .. cond .. ")) {\n" .. body .. "}"
				open_count = open_count + 1
			else
				cpp_code = cpp_code .. "if (is_lua_truthy(" .. cond .. ")) {\n" .. body .. "}"
			end
		elseif clause[1] == "elseif_clause" then
			ctx:capture_start()
			local cond = translate_node(ctx, clause[5][1], depth + 1)
			local pre = ctx:capture_end()
			local body = translate_node(ctx, clause[5][2], depth + 1, { no_braces = true })
			
			if pre ~= "" then
				cpp_code = cpp_code .. " else {\n" .. pre .. "if (is_lua_truthy(" .. cond .. ")) {\n" .. body .. "}"
				open_count = open_count + 1
			else
				cpp_code = cpp_code .. " else if (is_lua_truthy(" .. cond .. ")) {\n" .. body .. "}"
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
	local condition = translate_node(ctx, node[5][1], depth + 1)
	local pre_stmts = ctx:capture_end()
	
	local body = translate_node(ctx, node[5][2], depth + 1, { no_braces = true })
	
	if pre_stmts ~= "" then
		return prev_stmts .. "while (true) {\n" .. pre_stmts .. "if (!is_lua_truthy(" .. condition .. ")) break;\n" .. body .. "}\n"
	else
		return prev_stmts .. "while (is_lua_truthy(" .. condition .. ")) {\n" .. body .. "}\n"
	end
end)

register_handler("repeat_until_statement", function(ctx, node, depth)
	local prev_stmts = ctx:flush_statements()
	
	local block_node = node[5][1]
	local condition_node = node[5][2]
	
	local body = translate_node(ctx, block_node, depth + 1, { no_braces = true })
	
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
	return ctx:flush_statements() .. node[2] .. ":;\n"
end)

register_handler("goto_statement", function(ctx, node, depth)
	return ctx:flush_statements() .. "goto " .. node[2] .. ";\n"
end)


register_handler("return_statement", function(ctx, node, depth)
	local expr_list_node = node[5] and node[5][1]
	local cpp_code = ctx:flush_statements()
	
	if expr_list_node and #(expr_list_node[5] or empty_table) > 0 then
		for _, expr_node in ipairs(expr_list_node[5] or empty_table) do
			local val = translate_node(ctx, expr_node, depth + 1)
			local stmts = ctx:flush_statements()
			cpp_code = cpp_code .. stmts .. "out_result.push_back(" .. val .. ");\n"
		end
	end
	return cpp_code .. ctx.current_return_stmt .. "\n"
end)

--------------------------------------------------------------------------------
-- For Loop Handlers
--------------------------------------------------------------------------------

register_handler("for_numeric_statement", function(ctx, node, depth)
	local prev_stmts = ctx:flush_statements()
	
	local var_name = sanitize_cpp_identifier(node[5][1][3])
	local start_node = node[5][2]
	local end_node = node[5][3]
	
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
	
	if #(node[5] or empty_table) == 5 then
		step_node = node[5][4]
		step_expr_code = translate_node(ctx, step_node, depth + 1)
		step_stmts = ctx:flush_statements()
		body_node = node[5][5]
	else
		step_expr_code = nil
		body_node = node[5][4]
	end
	
	ctx:declare_variable(node[5][1][3])
	
	-- Optimization for integer loops (literals only)
	local all_integers = (start_node[1] == "integer") and (end_node[1] == "integer")
	local step_is_one = (step_node == nil) or (step_node[1] == "integer" and tonumber(step_node[2]) == 1)
	local step_is_neg_one = (step_node and step_node[1] == "integer" and tonumber(step_node[2]) == -1)
	local step_is_integer = (step_node == nil) or (step_node[1] == "integer")
	
	-- Combine hoisted statements (side effects of the expressions)
	local setup_code = prev_stmts .. "{\n" .. start_stmts .. end_stmts .. step_stmts
	
	if all_integers and step_is_one then
		local start_val = tostring(start_node[2])
		local end_val = tostring(end_node[2])
		-- Direct long long iteration, no inner LuaValue casting
		return setup_code .. "for (long long " .. var_name .. " = " .. start_val .. "LL; " .. var_name .. " <= " .. end_val .. "LL; ++" .. var_name .. ") {\n" ..
			translate_node(ctx, body_node, depth + 1, { no_braces = true }) .. "}}\n"

	elseif all_integers and step_is_neg_one then
		local start_val = tostring(start_node[2])
		local end_val = tostring(end_node[2])
		return setup_code .. "for (long long " .. var_name .. " = " .. start_val .. "LL; " .. var_name .. " >= " .. end_val .. "LL; --" .. var_name .. ") {\n" ..
			translate_node(ctx, body_node, depth + 1, { no_braces = true }) .. "}}\n"

	elseif all_integers and step_is_integer then
		local start_val = tostring(start_node[2])
		local end_val = tostring(end_node[2])
		local step_val = tostring(step_node[2])
		local condition = "(" .. step_val .. " >= 0 ? " .. var_name .. " <= " .. end_val .. "LL : " .. var_name .. " >= " .. end_val .. "LL)"
		return setup_code .. "for (long long " .. var_name .. " = " .. start_val .. "LL; " .. condition .. "; " .. var_name .. " += " .. step_val .. "LL) {\n" ..
			translate_node(ctx, body_node, depth + 1, { no_braces = true }) .. "}}\n"

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
			   translate_node(ctx, body_node, depth + 1, { no_braces = true }) .. "}}\n"
	end
end)

register_handler("for_generic_statement", function(ctx, node, depth)
	local prev_stmts = ctx:flush_statements()
	
	local var_list_node = node[5][1]
	local expr_list_node = node[5][2]
	local body_node = node[5][3]
	local loop_vars = {}
	
	for _, var_node in ipairs(var_list_node[5] or empty_table) do
		local sanitized_var = sanitize_cpp_identifier(var_node[3])
		table.insert(loop_vars, sanitized_var)
		ctx:declare_variable(var_node[3])
	end
	
	local iter_func_var = "iter_func_" .. ctx:get_unique_id()
	local iter_state_var = "iter_state_" .. ctx:get_unique_id()
	local iter_value_var = "iter_value_" .. ctx:get_unique_id()
	local results_var = "iter_results_" .. ctx:get_unique_id()
	local current_vals_var = "current_values_" .. ctx:get_unique_id()
	
	local cpp_code = prev_stmts .. "{\n" -- Open block for loop scope
	
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
	
	-- Create reusable buffer outside loop
	cpp_code = cpp_code .. "LuaValueVector " .. current_vals_var .. ";\n"
	cpp_code = cpp_code .. current_vals_var .. ".reserve(3);\n"
	
	cpp_code = cpp_code .. "while (true) {\n"
	cpp_code = cpp_code .. "    LuaValue args_obj[2] = {" .. iter_state_var .. ", " .. iter_value_var .. "};\n"
	
	-- Direct call to void function with output parameter
	cpp_code = cpp_code .. "    call_lua_value(" .. iter_func_var .. ", args_obj, 2, " .. current_vals_var .. ");\n"
	
	cpp_code = cpp_code .. "    if (" .. current_vals_var .. ".empty() || std::holds_alternative<std::monostate>(" .. current_vals_var .. "[0])) {\n"
	cpp_code = cpp_code .. "        break;\n"
	cpp_code = cpp_code .. "    }\n"
	
	for i, var_name in ipairs(loop_vars) do
		cpp_code = cpp_code .. "    LuaValue " .. var_name .. " = (" .. current_vals_var .. ".size() > " .. (i - 1) .. ") ? " .. current_vals_var .. "[" .. (i - 1) .. "] : std::monostate{};\n"
	end
	
	cpp_code = cpp_code .. "    " .. iter_value_var .. " = " .. loop_vars[1] .. ";\n"
	cpp_code = cpp_code .. translate_node(ctx, body_node, depth + 1, { no_braces = true }) .. "\n"
	cpp_code = cpp_code .. "}}\n"
	
	return cpp_code
end)

--------------------------------------------------------------------------------
-- CppTranslator Class Methods
--------------------------------------------------------------------------------

-- Helper function to emit cache declarations for library member accesses
local function emit_cache_declarations(ctx)
	local global_code = ""
	local local_code = ""
	
	-- 1. String Literals (Can be global)
	local sorted_strings = {}
	for s, _ in pairs(ctx.string_literals) do table.insert(sorted_strings, s) end
	table.sort(sorted_strings)
	for _, s in ipairs(sorted_strings) do
		local cache_var = ctx.string_literals[s]
		local escaped = s:gsub('\\', '\\\\'):gsub('"', '\\"'):gsub('\n', '\\n'):gsub('\t', '\\t'):gsub('\r', '\\r')
		-- We use LuaObject::intern to ensure that the string_view stored in LuaValue 
		-- points to the same memory address as keys stored in LuaObjects.
		-- This enables O(1) pointer equality checks in table lookups.
		global_code = global_code .. "static const LuaValue " .. cache_var .. " = LuaValue(LuaObject::intern(\"" .. escaped .. "\"));\n"
	end

	-- 2. Global Identifiers
	local sorted_globals = {}
	for name, _ in pairs(ctx.global_identifier_caches) do table.insert(sorted_globals, name) end
	table.sort(sorted_globals)
	for _, name in ipairs(sorted_globals) do
		local cache_var = ctx.global_identifier_caches[name]
		if name:match("^const_int_") then
			local val = name:match("^const_int_(%d+)$")
			global_code = global_code .. "static const LuaValue " .. cache_var .. " = LuaValue(static_cast<long long>(" .. val .. "));\n"
		else
			local_code = local_code .. "static const LuaValue " .. cache_var .. " = _G->get_item(\"" .. name .. "\");\n"
		end
	end

	-- 3. Library Members
	local sorted_lib_members = {}
	for k, _ in pairs(ctx.lib_member_caches) do table.insert(sorted_lib_members, k) end
	table.sort(sorted_lib_members)
	for _, cache_key in ipairs(sorted_lib_members) do
		local cache_var = ctx.lib_member_caches[cache_key]
		local lib, member = cache_key:match("^([^_]+)_(.+)$")
		if lib and member then
			if lib == "_G" then
				local_code = local_code .. "static const LuaValue " .. cache_var 
					.. " = _G->get_item(\"" .. member .. "\");\n"
			else
				-- Use the previously cached global for the library itself
				local_code = local_code .. "static const LuaValue " .. cache_var 
					.. " = get_object(_cache_global_" .. lib .. ")->get_item(\"" .. member .. "\");\n"
			end
		end
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
			local global_cache_decls, local_cache_decls = emit_cache_declarations(ctx)
			local buffer_decl = "    LuaValueVector out_result; out_result.reserve(10);\n" ..
			                    "    LuaValueVector " .. RET_BUF_NAME .. "; " .. RET_BUF_NAME .. ".reserve(10);\n"
			local main_function_start = "int main(int argc, char* argv[]) {\n" ..
										"init_G(argc, argv);\n" .. local_cache_decls .. buffer_decl
			local main_function_end = "\n    return 0;\n}"
			return header .. global_cache_decls .. main_function_start .. generated_code .. main_function_end
		end
	else
		local namespace_start = "namespace " .. file_name .. " {\n"
		local namespace_end = "\n} // namespace " .. file_name .. "\n"
		
		if for_header then
			local hpp_header = "#pragma once\n#include \"lua_object.hpp\"\n\n"
			local hpp_load_function_declaration = "LuaValueVector load();\n"
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
			local explicit_return_value = "LuaObject::create()"
			
			for _, child in ipairs(ast_root[5] or empty_table) do
				if child[1] == "local_declaration" and #(child[5] or empty_table) >= 2 and child[5][1][1] == "variable" and child[5][2][1] == "table_constructor" then
					module_identifier = child[5][1][3]
					module_body_code = module_body_code .. translate_node(ctx, child, 0) .. "\n"
				elseif child[1] == "return_statement" then
					explicit_return_found = true
					local return_expr_node = child[5][1]
					if return_expr_node then
						if return_expr_node[1] == "expression_list" then
							local return_values = {}
							for _, expr_node in ipairs(return_expr_node[5] or empty_table) do
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
				load_function_body = load_function_body .. "return {LuaObject::create()};\n"
			end
			
			-- Emit cache declarations for library member accesses at start of load function
			local global_cache_decls, local_cache_decls = emit_cache_declarations(ctx)
			local buffer_decl = "    LuaValueVector out_result; out_result.reserve(10);\n" ..
			                    "    LuaValueVector " .. RET_BUF_NAME .. "; " .. RET_BUF_NAME .. ".reserve(10);\n"
			local load_function_definition = "LuaValueVector load() {\n" .. local_cache_decls .. buffer_decl .. load_function_body .. "}\n"
			return header .. cpp_header .. global_cache_decls .. global_var_definitions .. namespace_start .. load_function_definition .. namespace_end
		end
	end
end

return CppTranslator