--[[ This script automates the translation and compilation of LuaX projects. ]]

local script_path = arg[0]
local script_dir = "/usr/lib/LuaX/"
package.path = package.path .. ";" .. script_dir .. "../?.lua;" .. script_dir .. "../?/init.lua"

local cpp_translator = require("src.cpp_translator")
local translator = require("src.translator")
local formatter = require("src.formatter")
local is_running_native = (_G._VERSION:find("LuaX") ~= nil)

-- Configuration Defaults
local BUILD_DIR = "build"
local CXX = "clang++"
local keep_files = false
local do_compile = true
local input_lua_file = nil
local path_to_out_file = nil

-- Argument Parsing
local function print_usage()
	local cmd = is_running_native and "luax" or "lua5.4 src/luax.lua"
	print(string.format([[
%s [options] <input_file.lua>

Options:
  -o, --output <file>    Path/name of the resulting executable (default: input name)
  -b, --build-dir <dir>  Directory for intermediate files (default: "build")
  -t, --translate-only   Only generate C++ files, do not compile.
  -k, --keep             Preserve generated source/object files after compilation.
  -h, --help             Show this help message.
]], cmd))
	os.exit(0)
end

local i = 1
while i <= #arg do
	local a = arg[i]
	if a == "-k" or a == "--keep" then
		keep_files = true
	elseif a == "-t" or a == "--translate-only" then
		do_compile = false
		keep_files = true -- Implicitly keep if we aren't compiling
	elseif a == "-o" or a == "--output" then
		path_to_out_file = arg[i+1]
		i = i + 1
	elseif a == "-b" or a == "--build-dir" then
		BUILD_DIR = arg[i+1]
		i = i + 1
	elseif a == "-h" or a == "--help" then
		print_usage()
	elseif not input_lua_file then
		input_lua_file = a
	end
	i = i + 1
end

if not input_lua_file then
	print("Error: No input file specified.")
	print_usage()
end

-- Default output name logic
if not path_to_out_file then
	path_to_out_file = input_lua_file:gsub("%.lua$", ""):gsub(".*/", "")
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
	print("Executing: " .. command_str)
	local success = os.execute(command_str)
	if not success and error_message then
		error(error_message)
	end
end

local function is_uptodate(src, dst)
	local cmd = "test -e " .. dst .. " && test " .. dst .. " -nt " .. src
	local success, _, exit_code = os.execute(cmd)
	if type(success) == "boolean" then return success and (exit_code == 0) end
	return success == 0
end

local function translate_file(lua_file_path, output_file_name, is_main_entry, should_format)
	local translate_object = cpp_translator:new()

	print("Translating " .. lua_file_path .. "...")
	local file = io.open(lua_file_path, "r")
	if not file then error("Could not open " .. lua_file_path) end
	local lua_code = file:read("*all")
	file:close()

	local ast = translator.translate(lua_code, lua_file_path)

	local cpp_code, hpp_code
	if is_main_entry then
		cpp_code = translate_object:translate_recursive(ast, output_file_name, false, nil, true)
		hpp_code = translate_object:translate_recursive(ast, output_file_name, true, nil, true)
	else
		cpp_code = translate_object:translate_recursive(ast, output_file_name, false, output_file_name, false)
		hpp_code = translate_object:translate_recursive(ast, output_file_name, true, output_file_name, false)
	end

	local cpp_output_path = BUILD_DIR .. "/" .. output_file_name .. ".cpp"
	local hpp_output_path = BUILD_DIR .. "/" .. output_file_name .. ".hpp"

	if should_format then
		cpp_code = formatter.format_cpp_code(cpp_code)
		hpp_code = formatter.format_cpp_code(hpp_code)
	end

	local function write_file(path, content)
		local f = io.open(path, "w")
		if not f then error("Could not write to " .. path) end
		f:write(content)
		f:close()
	end

	write_file(cpp_output_path, cpp_code)
	write_file(hpp_output_path, hpp_code)

	return lua_file_path
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
				local rel_path = module_name:gsub("%.", "/") .. ".lua"
				local path_from_current = current_dir .. rel_path
				local dep_path = io.open(path_from_current, "r") and path_from_current or rel_path
				table.insert(dependencies, {path = dep_path, name = module_name})
			end
		end
	end
	file:close()
	return dependencies
end

local function generate_cmake(output_path, generated_basenames)
	local cmake_path = BUILD_DIR .. "/CMakeLists.txt"
	local luax_root = get_abs_path(script_dir)
	local abs_target = get_abs_path(output_path)
	local target_dir = abs_target:match("(.*/)") or "./"
	local target_name = abs_target:match(".*/(.*)") or abs_target

	local lib_cpp_files = {
		"lib/lua_object.cpp", "lib/math.cpp", "lib/string.cpp", "lib/table.cpp",
		"lib/os.cpp", "lib/io.cpp", "lib/package.cpp", "lib/utf8.cpp",
		"lib/init.cpp", "lib/debug.cpp", "lib/coroutine.cpp"
	}

	local lib_srcs = {}
	for _, f in ipairs(lib_cpp_files) do table.insert(lib_srcs, '"' .. luax_root .. f .. '"') end

	local gen_srcs = {}
	for _, basename in ipairs(generated_basenames) do table.insert(gen_srcs, '"' .. basename .. ".cpp" .. '"') end

	local cmake_content = {
		"cmake_minimum_required(VERSION 3.10)",
		"project(LuaX_Generated_Project LANGUAGES CXX)",
		"set(CMAKE_CXX_STANDARD 17)",
		"add_compile_options(-O2)",
		"include_directories(\"" .. luax_root .. "include\")",
		"set(LIB_SOURCES " .. table.concat(lib_srcs, "\n    ") .. ")",
		"set(GEN_SOURCES " .. table.concat(gen_srcs, "\n    ") .. ")",
		"add_executable(" .. target_name .. " ${LIB_SOURCES} ${GEN_SOURCES})",
		"target_link_libraries(" .. target_name .. " stdc++fs)",
		"set_target_properties(" .. target_name .. " PROPERTIES RUNTIME_OUTPUT_DIRECTORY \"" .. target_dir .. "\")"
	}

	local file = io.open(cmake_path, "w")
	file:write(table.concat(cmake_content, "\n"))
	file:close()
end

local function run_cmake()
	run_command("cmake -S " .. BUILD_DIR .. " -B " .. BUILD_DIR, "CMake configuration failed.")
    run_command("cmake --build " .. BUILD_DIR .. " -j", "Compilation failed.")
end

-- ============================================================================
-- MAIN EXECUTION
-- ============================================================================

run_command("mkdir -p " .. BUILD_DIR)

local files_to_translate = {}
local queue = {input_lua_file}
local visited = {[input_lua_file] = true}
local main_basename = path_to_out_file:match("([^/]+)$") or "main"
files_to_translate[input_lua_file] = main_basename

local head = 1
while head <= #queue do
	local current_file = queue[head]
	head = head + 1
	local deps = find_dependencies(current_file)
	for _, dep in ipairs(deps) do
		if not visited[dep.path] then
			visited[dep.path] = true
			files_to_translate[dep.path] = dep.name:gsub("%.", "_")
			table.insert(queue, dep.path)
		end
	end
end

local generated_basenames = {}
for file_path, output_name in pairs(files_to_translate) do
	table.insert(generated_basenames, output_name)
	local cpp_out = BUILD_DIR .. "/" .. output_name .. ".cpp"

	if is_uptodate(file_path, cpp_out) then
		print("Up to date: " .. output_name)
	else
		translate_file(file_path, output_name, file_path == input_lua_file, true)
	end
end

generate_cmake(path_to_out_file, generated_basenames)

if do_compile then
	run_cmake()
	if not keep_files then
		run_command("rm -rf " .. BUILD_DIR)
		print("Cleaned up intermediate files.")
	end
	print("Compilation complete: " .. path_to_out_file)
else
	print("Transpilation complete. C++ files located in: " .. BUILD_DIR)
end