-- This script automates the translation and compilation of LuaX projects.
-- It takes a single Lua file as input, analyzes its dependencies, translates
-- all required Lua files into C++, generates a Makefile, and then uses
-- make to compile them into a single executable.

-- NEW: Make script's internal requires robust to the current working directory.
local script_path = arg[0]
local script_dir = script_path:match("(.*/)") or ""
package.path = package.path .. ";" .. script_dir .. "../?.lua;" .. script_dir .. "../?/init.lua"

local cpp_translator = require("src.cpp_translator")
local translator = require("src.translator")

-- Argument Parsing
local usage = "Usage: lua5.4 scripts/luax.lua [-k|--keep] <path_to_src_lua_file> <path_to_out_file>"
local input_lua_file = nil
local path_to_out_file = nil
local keep_files = false

for i = 1, #arg do
    local a = arg[i]
    if a == "-k" or a == "--keep" then
        keep_files = true
    elseif not input_lua_file then
        input_lua_file = a
    elseif not path_to_out_file then
        path_to_out_file = a
    end
end

if not input_lua_file or not path_to_out_file then
    print(usage)
    print("  -k, --keep   Preserve generated source and object files.")
    os.exit(1)
end

local function run_command(command_str, error_message)
    print("Executing command: " .. command_str)
    local success = os.execute(command_str)
    if not success and error_message then
        error(error_message)
    end
end

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
    
    if is_main_entry then
        cpp_translator.reset()
        cpp_code = cpp_translator.translate_recursive(ast, output_file_name, false, nil, true) 
        cpp_translator.reset()
        hpp_code = cpp_translator.translate_recursive(ast, output_file_name, true, nil, true)
    else
        cpp_translator.reset()
        cpp_code = cpp_translator.translate_recursive(ast, output_file_name, false, output_file_name, false) 
        cpp_translator.reset()
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
    local makefile_path = "build/Makefile"
    print("Generating " .. makefile_path .. "...")

    local lib_cpp_files = {
        "lib/lua_object.cpp", "lib/math.cpp", "lib/string.cpp", "lib/table.cpp",
        "lib/os.cpp", "lib/io.cpp", "lib/package.cpp", "lib/utf8.cpp",
        "lib/init.cpp", "lib/debug.cpp", "lib/coroutine.cpp"
    }

    -- Point to lib files relative to the build directory
    local lib_srcs_str = table.concat(lib_cpp_files, " "):gsub("lib/", "../lib/")

    local gen_srcs = {}
    for _, basename in ipairs(generated_basenames) do
        table.insert(gen_srcs, basename .. ".cpp")
    end
    local gen_srcs_str = table.concat(gen_srcs, " ")

    -- CHANGED: Makefile Logic
    -- 1. LIB_OBJS are generated in ../lib/. Make handles timestamps automatically.
    -- 2. clean_generated only removes the objects created from Lua translation.
    local content = {
        "CXX = clang++",
        "CXXFLAGS = -std=c++17 -I../include -g -O1",
        "LDFLAGS = -lstdc++fs",
        "TARGET = ../" .. output_path,
        "",
        "LIB_SRCS = " .. lib_srcs_str,
        "GEN_SRCS = " .. gen_srcs_str,
        "",
        -- These will resolve to ../lib/xxx.o. Make will check if ../lib/xxx.o exists 
        -- and is newer than ../lib/xxx.cpp. If so, it skips compilation.
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
        -- Clean everything (full reset)
        "clean:",
        "\trm -f $(TARGET) $(LIB_OBJS) $(GEN_OBJS)",
        "",
        -- Clean only the objects generated from Lua (keeping core lib objects)
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

    -- Run make from within the build directory
    run_command("make -j -C build", "Compilation failed.")
end

--- Cleans and recreates the build directory
run_command("mkdir -p build", "Failed to create build directory.")

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

local generated_basenames = {}
for file_path, output_name in pairs(files_to_translate) do
    table.insert(generated_basenames, output_name)
    local is_main_entry = (file_path == input_lua_file)
    translate_file(file_path, output_name, is_main_entry)
end

generate_and_run_makefile(path_to_out_file, generated_basenames, dep_graph, files_to_translate)

-- Cleanup Logic
if not keep_files then
    -- 1. Remove ONLY the objects generated from the Lua scripts. 
    -- We do NOT remove the LIB_OBJS (in ../lib/), so they are cached for the next run.
    run_command("make -C build clean_generated")
    
    -- 2. Remove the generated C++ source/headers and Makefile from build/
    run_command("rm -f build/*.cpp build/*.hpp build/Makefile")
    
    -- 3. Attempt to remove the build directory if empty
    os.execute("rmdir build 2>/dev/null")
    
    print("Cleaned up generated intermediate files (Core lib objects preserved).")
else
    print("Intermediate files preserved in 'build/' directory.")
end

print("Compilation complete. Executable at: " .. path_to_out_file)