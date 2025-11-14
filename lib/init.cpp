#include "init.hpp"
#include "debug.hpp"

void init_G() {
    _G->set("assert", std::make_shared<LuaFunctionWrapper>(lua_assert));
    _G->set("collectgarbage", std::make_shared<LuaFunctionWrapper>(lua_collectgarbage));
    _G->set("dofile", std::make_shared<LuaFunctionWrapper>(lua_dofile));
    _G->set("ipairs", std::make_shared<LuaFunctionWrapper>(lua_ipairs));
    _G->set("load", std::make_shared<LuaFunctionWrapper>(lua_load));
    _G->set("loadfile", std::make_shared<LuaFunctionWrapper>(lua_loadfile));
    _G->set("next", std::make_shared<LuaFunctionWrapper>(lua_next));
    _G->set("pairs", std::make_shared<LuaFunctionWrapper>(lua_pairs));
    _G->set("rawequal", std::make_shared<LuaFunctionWrapper>(lua_rawequal));
    _G->set("rawlen", std::make_shared<LuaFunctionWrapper>(lua_rawlen));
    _G->set("select", std::make_shared<LuaFunctionWrapper>(lua_select));
    _G->set("warn", std::make_shared<LuaFunctionWrapper>(lua_warn));
    _G->set("warn", std::make_shared<LuaFunctionWrapper>(lua_warn));
    _G->set("xpcall", std::make_shared<LuaFunctionWrapper>(lua_xpcall));
    _G->set("math", create_math_library());
    _G->set("string", create_string_library());
    _G->set("table", create_table_library());
    _G->set("os", create_os_library());
    _G->set("io", create_io_library());
    _G->set("package", create_package_library());
    _G->set("utf8", create_utf8_library());
    _G->set("debug", create_debug_library());
    _G->set("_VERSION", LuaValue(std::string("Lua 5.4")));
    _G->set("tonumber", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
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
    }));
    _G->set("tostring", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
        // tostring implementation
        return {to_cpp_string(args->get("1"))};
    }));
    _G->set("type", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
        // type implementation
        LuaValue val = args->get("1");
        if (std::holds_alternative<std::monostate>(val)) return {"nil"};
        if (std::holds_alternative<bool>(val)) return {"boolean"};
        if (std::holds_alternative<double>(val) || std::holds_alternative<long long>(val)) return {"number"};
        if (std::holds_alternative<std::string>(val)) return {"string"};
        if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) return {"table"};
        if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(val)) return {"function"};
        return {"unknown"};
    }));
    _G->set("getmetatable", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
        // getmetatable implementation
        LuaValue val = args->get("1");
        if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) {
            auto obj = std::get<std::shared_ptr<LuaObject>>(val);
            if (obj->metatable) {
                return {obj->metatable};
            }
        }
        return {std::monostate{}}; // nil
    }));
    _G->set("error", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
        // error implementation
        LuaValue message = args->get("1");
        throw std::runtime_error(to_cpp_string(message));
        return {std::monostate{}}; // Should not be reached
    }));
    _G->set("pcall", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
        // pcall implementation
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
    }));
}