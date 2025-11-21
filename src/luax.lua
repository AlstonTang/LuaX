-- This script automates the translation and compilation of LuaX projects.
-- It takes a single Lua file as input, analyzes its dependencies, translates
-- all required Lua files into C++, generates a Makefile, and then uses
-- make to compile them into a single executable.

-- NEW: Make script's internal requires robust to the current working directory.
-- We get the directory where this script is located and add the project root
-- (which is one level up from `scripts/`) to Lua's search path.
local script_path = arg[0]
local script_dir = script_path:match("(.*/)") or ""
package.path = package.path .. ";" .. script_dir .. "../?.lua;" .. script_dir .. "../?/init.lua"

-- NEW: Clear package.loaded to ensure the latest versions of our own modules are loaded.
-- This is good practice and implements the intention of the original comment.
package.loaded["src.cpp_translator"] = nil
package.loaded["src.translator"] = nil

-- CHANGED: Use module-style require, which is more standard and works with our new package.path.
local cpp_translator = require("src.cpp_translator")
local translator = require("src.translator")

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
    print("DEBUG: src/luax.lua lua_code type:", type(lua_code))
    if type(lua_code) == "string" then print("DEBUG: src/luax.lua lua_code length:", #lua_code) end

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

-- Helper to check if file exists
local function file_exists(path)
    local f = io.open(path, "r")
    if f then f:close() return true end
    return false
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
        -- Ignore comments
        if not line:match("^%s*%-%-") then
            -- Match require("module.name") or require('module.name')
        local module_name = line:match('require%s*%([\'"]([%w%._-]+)[\'"]%)')
        if module_name then
            local dep_path
            -- This logic correctly handles both `src.utils` and `utils` style requires
            local rel_path = module_name:gsub("%.", "/") .. ".lua"
            local path_from_current = current_dir .. rel_path
            local path_from_root = rel_path

            if file_exists(path_from_current) then
                dep_path = path_from_current
            elseif file_exists(path_from_root) then
                dep_path = path_from_root
            else
                -- Fallback to existing logic if neither found (or to produce a consistent error later)
                if module_name:find("%.") then
                    dep_path = path_from_root
                else
                    dep_path = path_from_current
                end
            end
            table.insert(dependencies, {path = dep_path, name = module_name})
        end
        end -- End if not comment
    end
    file:close()
    return dependencies
end

-- Function to generate and execute a Makefile
local function generate_and_run_makefile(output_path, generated_basenames, dep_graph, files_to_translate)
    local makefile_path = "build/Makefile"
    print("Generating " .. makefile_path .. "...")

    local lib_cpp_files = {
        "lib/lua_object.cpp", "lib/math.cpp", "lib/string.cpp", "lib/table.cpp",
        "lib/os.cpp", "lib/io.cpp", "lib/package.cpp", "lib/utf8.cpp",
        "lib/init.cpp", "lib/debug.cpp", "lib/coroutine.cpp"
    }

    -- Use ../ to reference files from the build directory
    local lib_srcs_str = table.concat(lib_cpp_files, " "):gsub("lib/", "../lib/")

    local gen_srcs = {}
    for _, basename in ipairs(generated_basenames) do
        table.insert(gen_srcs, basename .. ".cpp")
    end
    local gen_srcs_str = table.concat(gen_srcs, " ")

    local content = {
        "CXX = clang++",
        "CXXFLAGS = -std=c++17 -I../include -g -O2",
        "LDFLAGS = -lstdc++fs",
        "TARGET = ../" .. output_path, -- The final executable will be in the root or specified path
        "",
        "LIB_SRCS = " .. lib_srcs_str,
        "GEN_SRCS = " .. gen_srcs_str,
        "",
        "LIB_OBJS = $(LIB_SRCS:.cpp=.o)",
        "GEN_OBJS = $(GEN_SRCS:.cpp=.o)",
        "",
        ".PHONY: all clean",
        "",
        "all: $(TARGET)",
        "",
        "$(TARGET): $(LIB_OBJS) $(GEN_OBJS)",
        "\t$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)",
        "",
        -- Generic rule to compile any .cpp into a .o
        "%.o: %.cpp",
        "\t$(CXX) $(CXXFLAGS) -c -o $@ $<",
        "",
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

    table.insert(content, "")
    table.insert(content, "clean:")
    table.insert(content, "\trm -f $(TARGET) $(LIB_OBJS) $(GEN_OBJS)")

    local file = io.open(makefile_path, "w")
    if not file then
        error("Could not write to " .. makefile_path)
    end
    file:write(table.concat(content, "\n"))
    file:close()

    -- Run make from within the build directory
    run_command("make -j -C build", "Compilation failed.")
end


local usage = "Usage: lua5.4 scripts/luax.lua <path_to_src_lua_file> <path_to_out_file>"

-- Main script logic
local input_lua_file = arg[1]
if not input_lua_file then
    print(usage)
    os.exit(1)
end

local path_to_out_file = arg[2]
if not path_to_out_file then
    print(usage)
    os.exit(1)
end

--- Cleans and recreates the build directory
run_command("mkdir -p build", "Failed to create build directory.")

local files_to_translate = {}
local queue = {}
local visited = {}
local dep_graph = {} -- To store dependencies for the Makefile

-- Add initial file to queue and set
table.insert(queue, input_lua_file)
-- For the main file, we use the output filename provided by the user (stripped of extension)
local main_basename = path_to_out_file:match(".*/(.*)") or path_to_out_file
files_to_translate[input_lua_file] = main_basename
visited[input_lua_file] = true

local head = 1
while head <= #queue do
    local current_file = queue[head]
    head = head + 1

    local deps = find_dependencies(current_file)
    dep_graph[current_file] = deps -- Store dependencies for this file

    for _, dep in ipairs(deps) do
        local dep_file = dep.path
        local module_name = dep.name
        if not visited[dep_file] then
            visited[dep_file] = true
            -- Sanitize module name for filename (replace dots with underscores)
            local sanitized_name = module_name:gsub("%.", "_")
            files_to_translate[dep_file] = sanitized_name
            table.insert(queue, dep_file)
        end
    end
end

local generated_basenames = {}
-- Perform translation for all identified files
for file_path, output_name in pairs(files_to_translate) do
    table.insert(generated_basenames, output_name)
    
    local is_main_entry = (file_path == input_lua_file)
    
    translate_file(file_path, output_name, is_main_entry)
end

-- Generate the Makefile and run the compilation
generate_and_run_makefile(path_to_out_file, generated_basenames, dep_graph, files_to_translate)

print("Compilation complete. Executable at: " .. path_to_out_file)