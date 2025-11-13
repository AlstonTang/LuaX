#include "package.hpp"
#include "lua_object.hpp"
#include <string>
#include <sstream> // Added for std::stringstream
#include <fstream> // Added for std::ifstream
#include <numeric> // Added for std::accumulate
#include <vector> // Added for std::vector
using namespace std; // Temporarily added for debugging namespace issues

// package.config
std::vector<LuaValue> package_config(std::shared_ptr<LuaObject> args) {
    // Lua 5.4 package.config string:
    // field 1: directory separator (e.g., '/')
    // field 2: path separator (e.g., ';')
    // field 3: default path mark (e.g., '?')
    // field 4: replacement for default path mark (e.g., '.lua')
    // field 5: "binary" extension (e.g., '.so' or '.dll')
    return {LuaValue(std::string("/;?.lua;.so"))}; // Simplified for now
}

// package.path
std::vector<LuaValue> package_path(std::shared_ptr<LuaObject> args) {
    return {LuaValue(std::string(""))}; // For now, return an empty string
}

// package.searchpath
std::vector<LuaValue> package_searchpath(std::shared_ptr<LuaObject> args) {
    std::string name = to_cpp_string(args->get("1"));
    std::string path = to_cpp_string(args->get("2"));
    std::string sep = std::holds_alternative<std::string>(args->get("3")) ? std::get<std::string>(args->get("3")) : ".";
    std::string rep = std::holds_alternative<std::string>(args->get("4")) ? std::get<std::string>(args->get("4")) : "/";

    // Replace dots in name with replacement string
    std::string filename = name;
    size_t start_pos = 0;
    while((start_pos = filename.find(sep, start_pos)) != std::string::npos) {
        filename.replace(start_pos, sep.length(), rep);
        start_pos += rep.length();
    }

    std::string current_path_entry;
    std::stringstream ss(path);
    std::vector<std::string> tried_paths;

    while (std::getline(ss, current_path_entry, ';')) {
        std::string search_file = current_path_entry;
        size_t q_pos = search_file.find("?");
        if (q_pos != std::string::npos) {
            search_file.replace(q_pos, 1, filename);
        } else {
            search_file += "/" + filename; // Default if no '?'
        }

        // Check if file exists (simplified check for now)
        std::ifstream f(search_file);
        if (f.good()) {
            return {search_file};
        }
        tried_paths.push_back(search_file);
    }

    // If not found, return nil and a message with tried paths
    std::string error_msg = "no file '" + name + "' in path:\n\t" +
                            std::accumulate(tried_paths.begin(), tried_paths.end(), std::string(),
                                            [](const std::string& a, const std::string& b) {
                                                return a + "\t" + b + "\n";
                                            });
    return {std::monostate{}, error_msg}; // Return nil, error message would be second return value in Lua
}

// package.cpath
std::vector<LuaValue> package_cpath(std::shared_ptr<LuaObject> args) {
    return {LuaValue(std::string(""))}; // For now, return an empty string
}

// Global tables for package.loaded and package.preload
std::shared_ptr<LuaObject> package_loaded_table = std::make_shared<LuaObject>();
std::shared_ptr<LuaObject> package_preload_table = std::make_shared<LuaObject>();

std::shared_ptr<LuaObject> create_package_library() {
    auto package_lib = std::make_shared<LuaObject>();

    package_lib->set("config", std::make_shared<LuaFunctionWrapper>(package_config));
    package_lib->set("cpath", LuaValue(std::string("")));
    package_lib->set("loaded", package_loaded_table);
    package_lib->set("preload", package_preload_table);
    package_lib->set("searchpath", std::make_shared<LuaFunctionWrapper>(package_searchpath));
    package_lib->set("path", LuaValue(std::string("")));

    return package_lib;
}
