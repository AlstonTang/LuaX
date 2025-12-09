#include "init.hpp"

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
		{"tonumber", std::make_shared<LuaFunctionWrapper>([](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
			// tonumber implementation
			LuaValue val = args->get("1");
			if (std::holds_alternative<double>(val)) {
				return {val};
			} else if (std::holds_alternative<long long>(val)) {
				return {static_cast<double>(std::get<long long>(val))};
			} else if (std::holds_alternative<std::string>(val)) {
				std::string s = std::get<std::string>(val);
				try {
					// Check if the string contains only digits and an optional decimal point
					if (s.find_first_not_of("0123456789.") == std::string::npos) {
						return {std::stod(s)};
					}
				} catch (...) {
					// Fall through to return nil
				}
			}
			return {std::monostate{}}; // nil
		})},
		{"tostring", std::make_shared<LuaFunctionWrapper>([](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
			return {to_cpp_string(args->get("1"))};
		})},
		{"type", std::make_shared<LuaFunctionWrapper>([](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
			LuaValue val = args->get("1");
			if (std::holds_alternative<std::monostate>(val)) return {"nil"};
			if (std::holds_alternative<bool>(val)) return {"boolean"};
			if (std::holds_alternative<double>(val) || std::holds_alternative<long long>(val)) return {"number"};
			if (std::holds_alternative<std::string>(val)) return {"string"};
			if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) return {"table"};
			if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(val)) return {"function"};
			if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(val)) return {"thread"};
			return {"unknown"};
		})},
		{"getmetatable", std::make_shared<LuaFunctionWrapper>([](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
			LuaValue val = args->get("1");
			if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) {
				auto obj = std::get<std::shared_ptr<LuaObject>>(val);
				if (obj->metatable) {
					return {obj->metatable};
				}
			}
			return {std::monostate{}}; // nil
		})},
		{"error", std::make_shared<LuaFunctionWrapper>([](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
			LuaValue message = args->get("1");
			throw std::runtime_error(to_cpp_string(message));
			return {std::monostate{}}; // Should not be reached
		})},
		{"pcall", std::make_shared<LuaFunctionWrapper>([](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
			LuaValue func_to_call = args->get("1");
			if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(func_to_call)) {
				auto callable_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(func_to_call);
				auto func_args = std::make_shared<LuaObject>();
				for (int i = 2; ; ++i) {
					LuaValue arg = args->get(std::to_string(i));
					if (std::holds_alternative<std::monostate>(arg)) break;
					func_args->set(std::to_string(i - 1), arg);
				}
				try {
					std::vector<LuaValue> results_from_func = callable_func->func(func_args);
					std::vector<LuaValue> pcall_results;
					pcall_results.push_back(true);
					pcall_results.insert(pcall_results.end(), results_from_func.begin(), results_from_func.end());
					return pcall_results;
				} catch (const std::exception& e) {
					std::vector<LuaValue> pcall_results;
					pcall_results.push_back(false);
					pcall_results.push_back(LuaValue(e.what()));
					return pcall_results;
				} catch (...) {
					std::vector<LuaValue> pcall_results;
					pcall_results.push_back(false);
					pcall_results.push_back(LuaValue("An unknown C++ error occurred"));
					return pcall_results;
				}
			}
			return {false}; // Not a callable function
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