#include "init.hpp"

#include <memory>

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
	globals->properties = {
		{"assert", std::make_shared<LuaFunctionWrapper>(lua_assert)},
		{"collectgarbage", std::make_shared<LuaFunctionWrapper>(lua_collectgarbage)},
		{"dofile", std::make_shared<LuaFunctionWrapper>(lua_dofile)},
		{"ipairs", std::make_shared<LuaFunctionWrapper>(lua_ipairs)},
		{"load", std::make_shared<LuaFunctionWrapper>(lua_load)},
		{"loadfile", std::make_shared<LuaFunctionWrapper>(lua_loadfile)},
		{"next", std::make_shared<LuaFunctionWrapper>(lua_next)},
		{"pairs", std::make_shared<LuaFunctionWrapper>(lua_pairs)},
		{"rawequal", std::make_shared<LuaFunctionWrapper>(lua_rawequal)},
		{"rawlen", std::make_shared<LuaFunctionWrapper>(lua_rawlen)},
		{"rawget", std::make_shared<LuaFunctionWrapper>(lua_rawget)},
		{"rawset", std::make_shared<LuaFunctionWrapper>(lua_rawset)},
		{"select", std::make_shared<LuaFunctionWrapper>(lua_select)},
		{"warn", std::make_shared<LuaFunctionWrapper>(lua_warn)},
		{"xpcall", std::make_shared<LuaFunctionWrapper>(lua_xpcall)},
		{"math", create_math_library()},
		{"string", create_string_library()},
		{"table", create_table_library()},
		{"os", create_os_library()},
		{"io", create_io_library()},
		{"package", create_package_library()},
		{"utf8", create_utf8_library()},
		{"coroutine", create_coroutine_library()},
		{"debug", create_debug_library()},
		{"_VERSION", LuaValue(std::string("LuaX (Lua 5.4)"))},
		{"tonumber", std::make_shared<LuaFunctionWrapper>(lua_tonumber)},
		{"tostring", std::make_shared<LuaFunctionWrapper>([](const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) -> void {
			out.assign({to_cpp_string(args[0])}); return;
		})},
		{"type", std::make_shared<LuaFunctionWrapper>([](const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) -> void {
			LuaValue val = args[0];
			if (std::holds_alternative<std::monostate>(val)) {out.assign({"nil"}); return;};
			if (std::holds_alternative<bool>(val)) {out.assign({"boolean"}); return;};
			if (std::holds_alternative<double>(val) || std::holds_alternative<long long>(val)) {out.assign({"number"}); return;};
			if (std::holds_alternative<std::string>(val)) {out.assign({"string"}); return;};
			if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) {out.assign({"table"}); return;};
			if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(val)) {out.assign({"function"}); return;};
			if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(val)) {out.assign({"thread"}); return;};
			out.assign({"unknown"}); return;
		})},
		{"getmetatable", std::make_shared<LuaFunctionWrapper>([](const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) -> void {
			LuaValue val = args[0];
			if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) {
				auto obj = std::get<std::shared_ptr<LuaObject>>(val);
				if (obj->metatable) {
					out.assign({obj->metatable}); return;
				}
			}
			out.assign({std::monostate{}}); return; // nil
		})},
		{"error", std::make_shared<LuaFunctionWrapper>([](const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) -> void {
			LuaValue message = args[0];
			throw std::runtime_error(to_cpp_string(message));
		})},
		{"pcall", std::make_shared<LuaFunctionWrapper>([](const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) -> void {
			LuaValue func_to_call = args[0];
			if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(func_to_call)) {
				out.clear();
				auto callable_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(func_to_call);
				try {
					if (n_args > 1) {
						callable_func->func(args + 1, n_args - 1, out);
					} else {
						callable_func->func(nullptr, 0, out);
					}

					std::vector<LuaValue> pcall_results;
					out.insert(out.begin(), true);
				} catch (const std::exception& e) {
					std::vector<LuaValue> pcall_results;
					out.assign({false, LuaValue(e.what())});
				} catch (...) {
					std::vector<LuaValue> pcall_results;
					out.assign({false, LuaValue("An unknown C++ error occurred")});
				}
			}
			out.assign({false}); return; // Not a callable function
		})}
	};
	// Define _G inside _G
	globals->properties["_G"] = globals;
	return globals;
}

// Global variable definition, initialized at load time
std::shared_ptr<LuaObject> _G = create_initial_global();

void init_G(int argc, char* argv[]) {
	// Only handling dynamic arguments now
	auto arg = std::make_shared<LuaObject>();
	for (int i = 0; i < argc; i++) {
		arg->set_item(i, std::string(argv[i]));
	}
	_G->properties["arg"] = arg;
}