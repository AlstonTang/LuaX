#include "package.hpp"
#include "lua_object.hpp"
#include <string>
#include <sstream>
#include <fstream>
#include <numeric>
#include <vector>
#include <stdexcept>

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

// package.loadlib
std::vector<LuaValue> package_loadlib(std::shared_ptr<LuaObject> args) {
	throw std::runtime_error("package.loadlib is not supported in the translated environment.");
	return {}; // Should not be reached
}

// Global tables for package.loaded and package.preload
std::shared_ptr<LuaObject> package_loaded_table = std::make_shared<LuaObject>();
std::shared_ptr<LuaObject> package_preload_table = std::make_shared<LuaObject>();
std::shared_ptr<LuaObject> package_searchers_table = std::make_shared<LuaObject>();

std::shared_ptr<LuaObject> create_package_library() {
	static std::shared_ptr<LuaObject> package_lib;
	if (package_lib) return package_lib;

	package_lib = std::make_shared<LuaObject>();

	package_lib->properties = {
		{"config", LuaValue(std::string("/\n;\n?\n!\n-\n"))},
		#if defined(__linux__)
		{"cpath", LuaValue(std::string("/usr/local/lib/lua/5.4/?.so;/usr/lib/x86_64-linux-gnu/lua/5.4/?.so;/usr/lib/lua/5.4/?.so;/usr/local/lib/lua/5.4/loadall.so;./?.so"))}
		#elif defined(_WIN32)
		{"cpath", LuaValue(std::string(".\\?.dll;!.\\?.dll;!.\\loadall.dll"))}
		#else
		{"cpath", LuaValue(std::string("/?.so"))}
		#endif
		,
		{"loaded", package_loaded_table},
		{"loadlib", std::make_shared<LuaFunctionWrapper>(package_loadlib)},
		#if defined(__linux__)
		{"path", LuaValue(std::string("/usr/local/share/lua/5.4/?.lua;/usr/local/share/lua/5.4/?/init.lua;/usr/local/lib/lua/5.4/?.lua;/usr/local/lib/lua/5.4/?/init.lua;/usr/share/lua/5.4/?.lua;/usr/share/lua/5.4/?/init.lua;./?.lua;./?/init.lua"))}
		#elif defined(_WIN32)
		{"path", LuaValue(std::string(".;.\\?.lua;!\\lua\\?.lua;!\\lua\\?\\init.lua;C:\\Program Files\\Lua\\5.4\\?.lua;C:\\Program Files\\Lua\\5.4\\?\\init.lua"))}
		#else
		{"path", LuaValue(std::string("./?.lua;./?/init.lua"))}
		#endif
		,
		{"preload", package_preload_table},
		{"searchers", package_searchers_table},
		{"searchpath", std::make_shared<LuaFunctionWrapper>(package_searchpath)}
	};

	return package_lib;
}
