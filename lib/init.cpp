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

static std::shared_ptr<LuaObject> create_initial_global() {
	auto globals = std::make_shared<LuaObject>();
	
	globals->set("assert", std::make_shared<LuaFunctionWrapper>(lua_assert));
	globals->set("collectgarbage", std::make_shared<LuaFunctionWrapper>(lua_collectgarbage));
	globals->set("dofile", std::make_shared<LuaFunctionWrapper>(lua_dofile));
	globals->set("ipairs", std::make_shared<LuaFunctionWrapper>(lua_ipairs));
	globals->set("load", std::make_shared<LuaFunctionWrapper>(lua_load));
	globals->set("loadfile", std::make_shared<LuaFunctionWrapper>(lua_loadfile));
	globals->set("next", std::make_shared<LuaFunctionWrapper>(lua_next));
	globals->set("pairs", std::make_shared<LuaFunctionWrapper>(lua_pairs));
	globals->set("rawequal", std::make_shared<LuaFunctionWrapper>(lua_rawequal));
	globals->set("rawlen", std::make_shared<LuaFunctionWrapper>(lua_rawlen));
	globals->set("rawget", std::make_shared<LuaFunctionWrapper>(lua_rawget));
	globals->set("rawset", std::make_shared<LuaFunctionWrapper>(lua_rawset));
	globals->set("select", std::make_shared<LuaFunctionWrapper>(lua_select));
	globals->set("tonumber", std::make_shared<LuaFunctionWrapper>(lua_tonumber));
	globals->set("warn", std::make_shared<LuaFunctionWrapper>(lua_warn));
	
	globals->set("setmetatable", std::make_shared<LuaFunctionWrapper>([](const LuaValue* args, size_t n_args, LuaValueVector& out) {
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
	}));

	globals->set("getmetatable", std::make_shared<LuaFunctionWrapper>([](const LuaValue* args, size_t n_args, LuaValueVector& out) {
		if (n_args > 0) {
			if (auto obj = get_object(args[0])) {
				if (obj->metatable) {
					out.assign({obj->metatable});
					return;
				}
			}
		}
		out.assign({std::monostate{}});
	}));

	globals->set("type", std::make_shared<LuaFunctionWrapper>([](const LuaValue* args, size_t n_args, LuaValueVector& out) {
		if (n_args == 0) out.assign({LuaValue(std::string_view("no value"))});
		else out.assign({get_lua_type_name(args[0])});
	}));

	globals->set("tostring", std::make_shared<LuaFunctionWrapper>([](const LuaValue* args, size_t n_args, LuaValueVector& out) {
		if (n_args == 0) out.assign({LuaValue(std::string_view(""))});
		else out.assign({to_cpp_string(args[0])});
	}));

	globals->set("error", std::make_shared<LuaFunctionWrapper>([](const LuaValue* args, size_t n_args, LuaValueVector& out) {
		throw std::runtime_error(n_args > 0 ? to_cpp_string(args[0]) : "error");
	}));

	globals->set("pcall", std::make_shared<LuaFunctionWrapper>([](const LuaValue* args, size_t n_args, LuaValueVector& out) {
		if (n_args < 1) throw std::runtime_error("bad argument #1 to 'pcall' (value expected)");
		out.clear();
		try {
			if (const auto* func_ptr = std::get_if<std::shared_ptr<LuaCallable>>(&args[0])) {
				LuaValueVector call_res;
				(*func_ptr)->call(args + 1, n_args - 1, call_res);
				out.push_back(true);
				out.insert(out.end(), call_res.begin(), call_res.end());
				return;
			}
			out.assign({false, LuaValue(std::string_view("attempt to call a non-function value"))});
		} catch (const std::exception& e) {
			out.assign({false, LuaValue(std::string(e.what()))});
		}
	}));
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
	
	globals->set("print", std::make_shared<LuaFunctionWrapper>([](const LuaValue* args, size_t n_args, LuaValueVector& out) {
		for (size_t i = 0; i < n_args; i++) {
			std::cout << to_cpp_string(args[i]) << (i == n_args - 1 ? "" : "\t");
		}
		std::cout << std::endl;
		out.assign({std::monostate{}});
	}));

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
