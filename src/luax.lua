-- This script automates the translation and compilation of LuaX projects.
-- It takes a single Lua file as input, analyzes its dependencies, translates
-- all required Lua files into C++, compiles them into a single executable,
-- and then runs the executable.

-- Use dofile to ensure latest versions are loaded, bypassing require's cache
local cpp_translator = dofile("src/cpp_translator.lua")
local translator = dofile("src/translator.lua")

local function run_command(command_str, error_message)
    print("Executing command: " .. command_str)
    local success = os.execute(command_str)
    if not success and error_message then
        error(error_message)
    end
end

-- Updated function signature to accept is_main_entry flag
local function translate_file(lua_file_path, output_file_name, is_main_entry)
    print("Translating " .. lua_file_path .. "...")
    local file = io.open(lua_file_path, "r")
    if not file then
        error("Could not open " .. lua_file_path)
    end
    local lua_code = file:read("*all")
    file:close()

    local ast = translator.translate(lua_code)
    local cpp_code
    local hpp_code
    
    -- Check the flag instead of the output_file_name string
    if is_main_entry then
        -- Primary entry point: use global scope (nil for module name)
        cpp_code = cpp_translator.translate_recursive(ast, output_file_name, false, nil, true) 
        hpp_code = cpp_translator.translate_recursive(ast, output_file_name, true, nil, true)
    else
        -- Module: use module scope (output_file_name as module name)
        cpp_code = cpp_translator.translate_recursive(ast, output_file_name, false, output_file_name, false) 
        hpp_code = cpp_translator.translate_recursive(ast, output_file_name, true, output_file_name, false) 
    end

    local cpp_output_path = "build/" .. output_file_name .. ".cpp"
    local hpp_output_path = "build/" .. output_file_name .. ".hpp"

    local cpp_file = io.open(cpp_output_path, "w")
    if cpp_file then
        cpp_file:write(cpp_code)
        cpp_file:close()
        print("Generated " .. cpp_output_path)
    else
        error("Could not write to " .. cpp_output_path)
    end

    local hpp_file = io.open(hpp_output_path, "w")
    if hpp_file then
        hpp_file:write(hpp_code)
        hpp_file:close()
        print("Generated " .. hpp_output_path)
    else
        error("Could not write to " .. hpp_output_path)
    end
end

-- Function to find dependencies (simple require pattern matching)
local function find_dependencies(lua_file_path)
    local dependencies = {}
    local file = io.open(lua_file_path, "r")
    if not file then
        return dependencies -- No file, no dependencies
    end

    local current_dir = lua_file_path:match("(.*/)") or "" -- Extract directory of current file

    for line in file:lines() do
        local req_start_idx = string.find(line, 'require("', 1, true)
        if req_start_idx then
            local module_name_start = req_start_idx + string.len('require("')
            local req_end_idx = string.find(line, '")', module_name_start, true)
            if req_end_idx then
                local module_name = string.sub(line, module_name_start, req_end_idx - 1)
                
                local dep_path
                if module_name:find("%.") then -- If module name contains a dot, assume it's a path like src.module
                    dep_path = module_name:gsub("%.", "/") .. ".lua"
                else -- Otherwise, assume it's relative to the current file's directory
                    dep_path = current_dir .. module_name .. ".lua"
                end
                table.insert(dependencies, dep_path)
            end
        end
    end
    file:close()
    return dependencies
end

local usage = "Usage: lua5.4 scripts/luax.lua <path_to_src_lua_file> <path_to_out_file>"

-- Main script logic
local input_lua_file = arg[1]
if not input_lua_file then
    error(usage)
end

local path_to_out_file = arg[2]
if not path_to_out_file then
    error(usage)
end

--- Cleans and recreates the build directory
run_command("rm -r build")
run_command("mkdir -p build", "Failed to create build directory.")

local files_to_translate = {}
local queue = {}
local visited = {}

-- Add initial file to queue and set
table.insert(queue, input_lua_file)
files_to_translate[input_lua_file] = true
visited[input_lua_file] = true

local head = 1
while head <= #queue do
    local current_file = queue[head]
    head = head + 1

    local deps = find_dependencies(current_file)
    for _, dep_file in ipairs(deps) do
        if not visited[dep_file] then
            visited[dep_file] = true
            files_to_translate[dep_file] = true
            table.insert(queue, dep_file)
        end
    end
end

-- Perform translation for all identified files
for file_path, _ in pairs(files_to_translate) do
    local basename = file_path:match(".*/(.*)%.lua$") or file_path:match("(.*)%.lua$")
    if not basename then
        error("Invalid Lua file name for translation: " .. file_path)
    end
    
    local is_main_entry = (file_path == input_lua_file)
    
    -- Use the file's basename for the C++ file name for both main and modules
    translate_file(file_path, basename, is_main_entry)
end

-- Construct compilation command
local lib_cpp_files = {
    "lib/lua_object.cpp",
    "lib/math.cpp",
    "lib/string.cpp",
    "lib/table.cpp",
    "lib/os.cpp",
    "lib/io.cpp",
    "lib/package.cpp",
    "lib/utf8.cpp",
    "lib/init.cpp",
    "lib/debug.cpp",
    "lib/coroutine.cpp"
}

local compile_command = "clang++ -std=c++17 -Iinclude -o " .. path_to_out_file .. " "

-- Add all generated C++ files from the build directory
-- Dynamically find them as they might vary based on dependencies
local build_cpp_files_command = "find build -name '*.cpp'"
local build_cpp_files_handle = io.popen(build_cpp_files_command)
local build_cpp_files_str = build_cpp_files_handle:read("*all")
build_cpp_files_handle:close()

print("DEBUG: build_cpp_files_str: " .. build_cpp_files_str)


for file_path in build_cpp_files_str:gmatch("([^\n]+)") do
    compile_command = compile_command .. file_path .. " "
end

-- Add all C++ runtime library files
for _, file in ipairs(lib_cpp_files) do
    compile_command = compile_command .. file .. " "
end

compile_command = compile_command .. "-lstdc++fs -O0"

run_command(compile_command, "Compilation failed.")

print("Compiled.")