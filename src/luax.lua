--[[This script automates the translation and compilation of LuaX projects.
	It takes a single Lua file as input, analyzes its dependencies, translates
	all required Lua files into C++, generates a Makefile, and then uses
	make to compile them into a single executable.
]]

local script_path = arg[0]
local script_dir = "/usr/lib/LuaX/"
package.path = package.path .. ";" .. script_dir .. "../?.lua;" .. script_dir .. "../?/init.lua"

local cpp_translator = require("src.cpp_translator")
local translator = require("src.translator")
local formatter = require("src.formatter")
local is_running_native = (_G._VERSION:find("LuaX") ~= nil)

-- Configuration
local BUILD_DIR = "build" -- Change this to modify where intermediate files go
local CXX = "clang++"

-- Argument Parsing
local usage = (is_running_native and "Usage: luax" or "Usage: lua5.4 src/luax.lua") .. " [-k|--keep] <path_to_src_lua_file> <path_to_out_file> <build_directory>"
local input_lua_file = nil
local path_to_out_file = nil
local build_directory = nil
local keep_files = false

for i = 1, #arg do
	local a = arg[i]
	if a == "-k" or a == "--keep" then
		keep_files = true
	elseif not input_lua_file then
		input_lua_file = a
	elseif not path_to_out_file then
		path_to_out_file = a
	elseif a and not build_directory then
		BUILD_DIR = a
	end
end

if not input_lua_file or not path_to_out_file then
	print(usage)
	print("  -k, --keep   Preserve generated source and object files.")
	os.exit(1)
end

-- Helper: Get current working directory (Unix specific)
local function get_cwd()
	local p = io.popen("pwd")
	local cwd = p:read("*l")
	p:close()
	return cwd
end

-- Helper: Convert path to absolute path
local function get_abs_path(path)
	if path:sub(1, 1) == "/" then return path end
	return get_cwd() .. "/" .. path
end

local function run_command(command_str, error_message)
	print("Executing command: " .. command_str)
	local success = os.execute(command_str)
	if not success and error_message then
		error(error_message)
	end
end

local function is_uptodate(src, dst)
	-- Uses Unix 'test' command:
	-- -e: exists
	-- -nt: newer than
	local cmd = "test -e " .. dst .. " && test " .. dst .. " -nt " .. src
	local success, exit_type, exit_code = os.execute(cmd)
	
	if type(success) == "boolean" then
		return success and (exit_code == 0)
	elseif type(success) == "number" then
		return success == 0
	end
	return false
end

local function translate_file(lua_file_path, output_file_name, is_main_entry, should_format)
	local translate_object = cpp_translator:new()
	
	print("Translating " .. lua_file_path .. " at " .. os.clock() .. "...")
	local file = io.open(lua_file_path, "r")
	if not file then
		error("Could not open " .. lua_file_path)
	end
	local lua_code = file:read("*all")
	file:close()
	
	local ast = translator.translate(lua_code, lua_file_path)
	local cpp_code
	local hpp_code
	
    print("Generating CPP and HPP code for " .. lua_file_path .. " at " .. os.clock() .. "...")
	if is_main_entry then
		cpp_code = translate_object:translate_recursive(ast, output_file_name, false, nil, true)
		hpp_code = translate_object:translate_recursive(ast, output_file_name, true, nil, true)
	else
		cpp_code = translate_object:translate_recursive(ast, output_file_name, false, output_file_name, false)
		hpp_code = translate_object:translate_recursive(ast, output_file_name, true, output_file_name, false) 
	end

	-- Files are generated inside BUILD_DIR
	local cpp_output_path = BUILD_DIR .. "/" .. output_file_name .. ".cpp"
	local hpp_output_path = BUILD_DIR .. "/" .. output_file_name .. ".hpp"

	if should_format then
        print("Formatting CPP and HPP code for " .. lua_file_path .. " at " .. os.clock() .. "...")
		cpp_code = formatter.format_cpp_code(cpp_code)
		hpp_code = formatter.format_cpp_code(hpp_code)
	end

    print("Writing CPP and HPP code for " .. lua_file_path .. " at " .. os.clock() .. "...")
	local cpp_file = io.open(cpp_output_path, "w")
	if cpp_file then
		cpp_file:write(cpp_code)
		cpp_file:close()
	else
		error("Could not write to " .. cpp_output_path)
	end

	local hpp_file = io.open(hpp_output_path, "w")
	if hpp_file then
		hpp_file:write(hpp_code)
		hpp_file:close()
	else
		error("Could not write to " .. hpp_output_path)
	end

    print("Completed transpilation of " .. lua_file_path .. " at " .. os.clock() .. ".")
	
	return lua_file_path
end

local function file_exists(path)
	local f = io.open(path, "r")
	if f then f:close() return true end
	return false
end

local function find_dependencies(lua_file_path)
	local dependencies = {}
	local file = io.open(lua_file_path, "r")
	if not file then return dependencies end

	local current_dir = lua_file_path:match("(.*/)") or ""

	for line in file:lines() do
		if not line:match("^%s*%-%-") then
			local module_name = line:match('require%s*%([\'"]([%w%._-]+)[\'"]%)')
			if module_name then
				local dep_path
				local rel_path = module_name:gsub("%.", "/") .. ".lua"
				local path_from_current = current_dir .. rel_path
				local path_from_root = rel_path

				if file_exists(path_from_current) then
					dep_path = path_from_current
				elseif file_exists(path_from_root) then
					dep_path = path_from_root
				else
					if module_name:find("%.") then
						dep_path = path_from_root
					else
						dep_path = path_from_current
					end
				end
				table.insert(dependencies, {path = dep_path, name = module_name})
			end
		end
	end
	file:close()
	return dependencies
end

local function generate_and_run_makefile(output_path, generated_basenames, dep_graph, files_to_translate)
	local makefile_path = BUILD_DIR .. "/Makefile"
	print("Generating " .. makefile_path .. "...")

	-- Calculate Absolute Paths for Robustness
	-- We assume the Luax Library structure is relative to this script:
	-- script_dir/../lib/ and script_dir/../include/
	local luax_root = get_abs_path(script_dir)
	
	-- The output target should also be absolute so Make can find it from inside BUILD_DIR
	local abs_target = get_abs_path(output_path)

	local lib_cpp_files = {
		"lib/lua_object.cpp", "lib/math.cpp", "lib/string.cpp", "lib/table.cpp",
		"lib/os.cpp", "lib/io.cpp", "lib/package.cpp", "lib/utf8.cpp",
		"lib/init.cpp", "lib/debug.cpp", "lib/coroutine.cpp"
	}

	-- Construct absolute paths for lib sources
	local lib_srcs = {}
	for _, f in ipairs(lib_cpp_files) do
		table.insert(lib_srcs, luax_root .. f)
	end
	local lib_srcs_str = table.concat(lib_srcs, " ")

	local gen_srcs = {}
	for _, basename in ipairs(generated_basenames) do
		table.insert(gen_srcs, basename .. ".cpp")
	end
	local gen_srcs_str = table.concat(gen_srcs, " ")

	local content = {
		"CXX = " .. CXX,
		-- Include path is absolute to LUAX_HOME
		"CXXFLAGS = -std=c++17 -I" .. luax_root .. "include -O2",
		"LDFLAGS = -lstdc++fs",
		"TARGET = " .. abs_target,
		"",
		"LIB_SRCS = " .. lib_srcs_str,
		"GEN_SRCS = " .. gen_srcs_str,
		"",
		"LIB_OBJS = $(LIB_SRCS:.cpp=.o)", 
		"GEN_OBJS = $(GEN_SRCS:.cpp=.o)",
		"",
		".PHONY: all clean clean_generated",
		"",
		"all: $(TARGET)",
		"",
		"$(TARGET): $(LIB_OBJS) $(GEN_OBJS)",
		"\t$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)",
		"",
		"%.o: %.cpp",
		"\t$(CXX) $(CXXFLAGS) -c -o $@ $<",
		"",
		"clean:",
		"\trm -f $(TARGET) $(LIB_OBJS) $(GEN_OBJS)",
		"",
		"clean_generated:",
		"\trm -f $(GEN_OBJS)"
	}

	for lua_path, deps in pairs(dep_graph) do
		local basename = files_to_translate[lua_path]
		if basename then
			local obj_file = basename .. ".o"
			local hpp_deps = {}
			for _, dep in ipairs(deps) do
				local dep_name = dep.name
				local sanitized_dep_name = dep_name:gsub("%.", "_")
				table.insert(hpp_deps, sanitized_dep_name .. ".hpp")
			end
			if #hpp_deps > 0 then
				table.insert(content, obj_file .. ": " .. table.concat(hpp_deps, " "))
			end
		end
	end

	local file = io.open(makefile_path, "w")
	if not file then
		error("Could not write to " .. makefile_path)
	end
	file:write(table.concat(content, "\n"))
	file:close()

	run_command("make -j -C " .. BUILD_DIR, "Compilation failed.")
end

--- Cleans and recreates the build directory
run_command("mkdir -p " .. BUILD_DIR, "Failed to create build directory.")

local files_to_translate = {}
local queue = {}
local visited = {}
local dep_graph = {}

table.insert(queue, input_lua_file)
local main_basename = path_to_out_file:match(".*/(.*)") or path_to_out_file
files_to_translate[input_lua_file] = main_basename
visited[input_lua_file] = true

local head = 1
while head <= #queue do
	local current_file = queue[head]
	head = head + 1

	local deps = find_dependencies(current_file)
	dep_graph[current_file] = deps

	for _, dep in ipairs(deps) do
		local dep_file = dep.path
		local module_name = dep.name
		if not visited[dep_file] then
			visited[dep_file] = true
			local sanitized_name = module_name:gsub("%.", "_")
			files_to_translate[dep_file] = sanitized_name
			table.insert(queue, dep_file)
		end
	end
end

-- ============================================================================
-- TRANSLATION PHASE (Parallel + Incremental)
-- ============================================================================

local generated_basenames = {}
local threads = {}
local has_parallel = false --(type(coroutine.create_parallel) == "function")

if has_parallel then
	print("Parallel translation enabled.")
else
	print("Parallel translation unavailable (bootstrapping). Running sequentially.")
end

for file_path, output_name in pairs(files_to_translate) do
	table.insert(generated_basenames, output_name)
	local is_main_entry = (file_path == input_lua_file)
	local cpp_out = BUILD_DIR .. "/" .. output_name .. ".cpp"

	-- INCREMENTAL CHECK
	if is_uptodate(file_path, cpp_out) then
		print("Up to date: " .. output_name)
		goto continue
	end

	if has_parallel then
		local co = coroutine.create_parallel(function(fp, out, main, keep) 
			return translate_file(fp, out, main, keep) 
		end)
		coroutine.resume(co, file_path, output_name, is_main_entry, keep_files)
		table.insert(threads, co)
	else
		translate_file(file_path, output_name, is_main_entry, keep_files)
	end

	::continue::
end

-- Synchronization Barrier
if has_parallel and #threads > 0 then
	local error_count = 0
	for _, co in ipairs(threads) do
		local success, val = coroutine.await(co) 
		
		if not success then
			print("Error in thread: " .. tostring(val))
			error_count = error_count + 1
		end
	end
	
	if error_count > 0 then
		error("Compilation failed with " .. error_count .. " errors.")
	end
end

-- ============================================================================

generate_and_run_makefile(path_to_out_file, generated_basenames, dep_graph, files_to_translate)

-- Cleanup Logic
if not keep_files then
	run_command("make -C " .. BUILD_DIR .. " clean_generated")
	run_command("rm -f " .. BUILD_DIR .. "/*.cpp " .. BUILD_DIR .. "/*.hpp " .. BUILD_DIR .. "/Makefile")
	os.execute("rmdir " .. BUILD_DIR .. " 2>/dev/null")
	print("Cleaned up generated intermediate files.")
else
	print("Intermediate files preserved in '" .. BUILD_DIR .. "/' directory.")
end

print("Compilation complete. Executable at: " .. path_to_out_file)