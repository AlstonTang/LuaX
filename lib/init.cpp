#include "init.hpp"

#include <memory>
#include <iostream>
#include <vector>

#include "coroutine.hpp"
#include "debug.hpp"
#include "io.hpp"
#include "lua_object.hpp"
#include "math.hpp"
#include "os.hpp"
#include "package.hpp"
#include "string.hpp"
#include "table.hpp"
#include "utf8.hpp"

// --- Global Functions ---

void lua_setmetatable(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 2) throw std::runtime_error("bad argument #2 to 'setmetatable' (nil or table expected)");
	if (auto obj = get_object(args[0])) {
		if (std::holds_alternative<std::monostate>(args[1])) {
			obj->set_metatable(nullptr);
		} else if (auto mt = get_object(args[1])) {
			obj->set_metatable(mt);
		}
		out.assign({args[0]});
		return;
	}
	throw std::runtime_error("bad argument #1 to 'setmetatable' (table expected)");
}

void lua_getmetatable(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args > 0) {
		if (auto obj = get_object(args[0])) {
			if (obj->metatable) {
				out.assign({obj->metatable});
				return;
			}
		}
	}
	out.assign({std::monostate{}});
}

void lua_type(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args == 0) out.assign({LuaValue(std::string_view("no value"))});
	else out.assign({get_lua_type_name(args[0])});
}

void lua_tostring(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args == 0) out.assign({LuaValue(std::string_view(""))});
	else out.assign({to_cpp_string(args[0])});
}

void lua_error(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error(n_args > 0 ? to_cpp_string(args[0]) : "error");
}

void lua_pcall(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 1) throw std::runtime_error("bad argument #1 to 'pcall' (value expected)");
	out.clear();
	try {
        LuaValueVector call_res;
        call_lua_value(args[0], args + 1, n_args - 1, call_res);
        out.reserve(call_res.size() + 1);
        out.push_back(true);
        out.insert(out.end(), call_res.begin(), call_res.end());
	} catch (const std::exception& e) {
		out.assign({false, LuaValue(std::string(e.what()))});
	}
}

void lua_print(const LuaValue* args, size_t n_args, LuaValueVector& out) {
    for (size_t i = 0; i < n_args; i++) {
        std::cout << to_cpp_string(args[i]) << (i == n_args - 1 ? "" : "\t");
    }
    std::cout << std::endl;
    out.assign({std::monostate{}});
}

static std::shared_ptr<LuaObject> create_initial_global() {
	auto globals = std::make_shared<LuaObject>();
	
	globals->set("assert", LUA_C_FUNC(lua_assert));
	globals->set("collectgarbage", LUA_C_FUNC(lua_collectgarbage));
	globals->set("dofile", LUA_C_FUNC(lua_dofile));
	globals->set("ipairs", LUA_C_FUNC(lua_ipairs));
	globals->set("load", LUA_C_FUNC(lua_load));
	globals->set("loadfile", LUA_C_FUNC(lua_loadfile));
	globals->set("next", LUA_C_FUNC(lua_next));
	globals->set("pairs", LUA_C_FUNC(lua_pairs));
	globals->set("rawequal", LUA_C_FUNC(lua_rawequal));
	globals->set("rawlen", LUA_C_FUNC(lua_rawlen));
	globals->set("rawget", LUA_C_FUNC(lua_rawget));
	globals->set("rawset", LUA_C_FUNC(lua_rawset));
	globals->set("select", LUA_C_FUNC(lua_select));
	globals->set("tonumber", LUA_C_FUNC(lua_tonumber));
	globals->set("warn", LUA_C_FUNC(lua_warn));
	
	globals->set("setmetatable", LUA_C_FUNC(lua_setmetatable));
	globals->set("getmetatable", LUA_C_FUNC(lua_getmetatable));
	globals->set("type", LUA_C_FUNC(lua_type));
	globals->set("tostring", LUA_C_FUNC(lua_tostring));
	globals->set("error", LUA_C_FUNC(lua_error));
	globals->set("pcall", LUA_C_FUNC(lua_pcall));
	globals->set("_VERSION", std::string("Lua 5.4"));
	globals->set("math", create_math_library());
	globals->set("string", create_string_library());
	globals->set("table", create_table_library());
	globals->set("os", create_os_library());
	globals->set("io", create_io_library());
	globals->set("package", create_package_library());
	globals->set("coroutine", create_coroutine_library());
	globals->set("utf8", create_utf8_library());
	globals->set("debug", create_debug_library());
	globals->set("_G", globals);
	
	globals->set("print", LUA_C_FUNC(lua_print));

	return globals;
}

std::shared_ptr<LuaObject> _G = create_initial_global();

void init_G(int argc, char* argv[]) {
	auto arg = std::make_shared<LuaObject>();
	for (int i = 0; i < argc; i++) {
		arg->set_item((double)(i), std::string(argv[i]));
	}
	_G->set("arg", arg);
}
