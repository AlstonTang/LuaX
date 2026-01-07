#include "package.hpp"
#include "lua_object.hpp"
#include <string>
#include <sstream>
#include <fstream>
#include <numeric>
#include <vector>
#include <stdexcept>

// package.searchpath
void package_searchpath(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	std::string name = to_cpp_string(args[0]);
	std::string path = to_cpp_string(args[1]);
	std::string sep = n_args >= 3 && std::holds_alternative<std::string>(args[2])
		                  ? std::get<std::string>(args[2])
		                  : ".";
	std::string rep = n_args >= 4 && std::holds_alternative<std::string>(args[3])
		                  ? std::get<std::string>(args[3])
		                  : "/";

	// Replace dots in name with replacement string
	std::string filename = name;
	size_t start_pos = 0;
	while ((start_pos = filename.find(sep, start_pos)) != std::string::npos) {
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
		}
		else {
			search_file += "/" + filename; // Default if no '?'
		}

		// Check if file exists (simplified check for now)
		std::ifstream f(search_file);
		if (f.good()) {
			out.assign({search_file});
			return;
		}
		tried_paths.push_back(search_file);
	}

	// If not found, return nil and a message with tried paths
	std::string error_msg = "no file '" + name + "' in path:\n\t" +
		std::accumulate(tried_paths.begin(), tried_paths.end(), std::string(),
		                [](const std::string& a, const std::string& b) {
			                return a + "\t" + b + "\n";
		                });
	out.assign({std::monostate{}, error_msg});
	return; // Return nil, error message would be second return value in Lua
}

// package.loadlib
void package_loadlib(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("package.loadlib is not supported in the translated environment.");
}

// Global tables for package.loaded and package.preload
std::shared_ptr<LuaObject> package_loaded_table = std::make_shared<LuaObject>();
std::shared_ptr<LuaObject> package_preload_table = std::make_shared<LuaObject>();
std::shared_ptr<LuaObject> package_searchers_table = std::make_shared<LuaObject>();

std::shared_ptr<LuaObject> create_package_library() {
	static std::shared_ptr<LuaObject> package_lib;
	if (package_lib) return package_lib;

	package_lib = LuaObject::create({
		{LuaValue(std::string_view("config")), std::string("/\n;\n?\n!\n-\n")},
#if defined(__linux__)
		{
			LuaValue(std::string_view("cpath")),
			std::string("/usr/local/lib/lua/5.4/?.so;/usr/lib/x86_64-linux-gnu/lua/5.4/?.so;/usr/lib/lua/5.4/?.so;/usr/local/lib/lua/5.4/loadall.so;./?.so")
		},
#elif defined(_WIN32)
		{ LuaValue(std::string_view("cpath")), std::string(".\\?.dll;!.\\?.dll;!.\\loadall.dll") },
#else
		{ LuaValue(std::string_view("cpath")), std::string("/?.so") },
#endif
		{LuaValue(std::string_view("loaded")), package_loaded_table},
		{LuaValue(std::string_view("loadlib")), std::make_shared<LuaFunctionWrapper>(package_loadlib)},
#if defined(__linux__)
		{
			LuaValue(std::string_view("path")),
			std::string("/usr/local/share/lua/5.4/?.lua;/usr/local/share/lua/5.4/?/init.lua;/usr/local/lib/lua/5.4/?.lua;/usr/local/lib/lua/5.4/?/init.lua;/usr/share/lua/5.4/?.lua;/usr/share/lua/5.4/?/init.lua;./?.lua;./?/init.lua")
		},
#elif defined(_WIN32)
		{
			LuaValue(std::string_view("path")), std::string(".;.\\?.lua;!\\lua\\?.lua;!\\lua\\?\\init.lua;C:\\Program Files\\Lua\\5.4\\?.lua;C:\\Program Files\\Lua\\5.4\\?\\init.lua")
		},
#else
		{ LuaValue(std::string_view("path")), std::string("./?.lua;./?/init.lua") },
#endif
		{LuaValue(std::string_view("preload")), package_preload_table},
		{LuaValue(std::string_view("searchers")), package_searchers_table},
		{LuaValue(std::string_view("searchpath")), std::make_shared<LuaFunctionWrapper>(package_searchpath)}
	});

	return package_lib;
}
