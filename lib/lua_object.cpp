#include "lua_object.hpp"
#include <iostream>
#include <variant>

std::shared_ptr<LuaObject> _G = std::make_shared<LuaObject>();

LuaValue LuaObject::get(const std::string& key) {
    if (properties.count(key)) {
        return properties[key];
    }
    if (metatable) {
        auto index = metatable->get("__index");
        if (std::holds_alternative<std::shared_ptr<LuaObject>>(index)) {
            return std::get<std::shared_ptr<LuaObject>>(index)->get(key);
        } else if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(index)) {
            // If __index is a function, call it
            auto args = std::make_shared<LuaObject>();
            args->set("1", shared_from_this());
            args->set("2", key);
            std::vector<LuaValue> results = std::get<std::shared_ptr<LuaFunctionWrapper>>(index)->func(args);
            if (!results.empty()) {
                return results[0];
            }
        }
    }
    return std::monostate{}; // nil
}

void LuaObject::set(const std::string& key, const LuaValue& value) {
    if (metatable) {
        auto newindex = metatable->get("__newindex");
        if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(newindex)) {
            // If __newindex is a function, call it
            auto args = std::make_shared<LuaObject>();
            args->set("1", shared_from_this());
            args->set("2", key);
            args->set("3", value);
            std::get<std::shared_ptr<LuaFunctionWrapper>>(newindex)->func(args);
            return;
        } else if (std::holds_alternative<std::shared_ptr<LuaObject>>(newindex)) {
            // If __newindex is a table, set the value in that table
            std::get<std::shared_ptr<LuaObject>>(newindex)->set(key, value);
            return;
        }
    }
    properties[key] = value;
}

void LuaObject::set_metatable(std::shared_ptr<LuaObject> mt) {
    metatable = mt;
}

std::shared_ptr<LuaObject> get_object(const LuaValue& value) {
    if (std::holds_alternative<std::shared_ptr<LuaObject>>(value)) {
        return std::get<std::shared_ptr<LuaObject>>(value);
    }
    return nullptr;
}

void print_value(const LuaValue& value) {
    if (std::holds_alternative<double>(value)) {
        std::cout << std::get<double>(value);
    } else if (std::holds_alternative<long long>(value)) {
        std::cout << std::get<long long>(value);
    } else if (std::holds_alternative<std::string>(value)) {
        std::cout << std::get<std::string>(value);
    } else if (std::holds_alternative<bool>(value)) {
        std::cout << (std::get<bool>(value) ? "true" : "false");
    } else if (std::holds_alternative<std::shared_ptr<LuaObject>>(value)) {
        std::cout << "<table>";
    } else {
        std::cout << "nil";
    }
}

double get_double(const LuaValue& value) {
    if (std::holds_alternative<double>(value)) {
        return std::get<double>(value);
    } else if (std::holds_alternative<long long>(value)) {
        return static_cast<double>(std::get<long long>(value));
    }
    return 0.0;
}

long long get_long_long(const LuaValue& value) {
    if (std::holds_alternative<long long>(value)) {
        return std::get<long long>(value);
    }
    return 0;
}

std::string to_cpp_string(const LuaValue& value) {
    if (std::holds_alternative<double>(value)) {
        double d = std::get<double>(value);
        if (d == static_cast<long long>(d)) {
            return std::to_string(static_cast<long long>(d));
        } else {
            return std::to_string(d);
        }
    } else if (std::holds_alternative<long long>(value)) { // Added this case
        return std::to_string(std::get<long long>(value));
    } else if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    } else if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "true" : "false";
    } else if (std::holds_alternative<std::shared_ptr<LuaObject>>(value)) {
        return "<table>";
    } else {
        return "nil";
    }
}

LuaValue rawget(std::shared_ptr<LuaObject> table, const LuaValue& key) {
    std::string key_str = to_cpp_string(key);
    if (table && table->properties.count(key_str)) {
        return table->properties[key_str];
    }
    return std::monostate{};
}


bool operator<=(const LuaValue& lhs, const LuaValue& rhs) {
    if (std::holds_alternative<double>(lhs) && std::holds_alternative<double>(rhs)) {
        return std::get<double>(lhs) <= std::get<double>(rhs);
    }
    return false;
}

// Helper for Lua-style equality comparison
bool lua_equals(const LuaValue& a, const LuaValue& b) {
    // Lua's equality rules:
    // - If types are different, always false (except for numbers and strings that can be converted)
    // - nil == nil is true
    // - numbers: compare values
    // - strings: compare values
    // - booleans: compare values
    // - tables/functions: compare by reference (pointers)

    if (a.index() != b.index()) {
        // Special case for numbers and strings that can be converted
        if (std::holds_alternative<double>(a) && std::holds_alternative<std::string>(b)) {
            try { return std::get<double>(a) == std::stod(std::get<std::string>(b)); } catch (...) { return false; }
        }
        if (std::holds_alternative<std::string>(a) && std::holds_alternative<double>(b)) {
            try { return std::stod(std::get<std::string>(a)) == std::get<double>(b); } catch (...) { return false; }
        }
        return false; // Different types, not equal
    }

    // Same types
    if (std::holds_alternative<std::monostate>(a)) {
        return true; // nil == nil
    } else if (std::holds_alternative<bool>(a)) {
        return std::get<bool>(a) == std::get<bool>(b);
    } else if (std::holds_alternative<double>(a)) {
        return std::get<double>(a) == std::get<double>(b);
    } else if (std::holds_alternative<std::string>(a)) {
        return std::get<std::string>(a) == std::get<std::string>(b);
    } else if (std::holds_alternative<std::shared_ptr<LuaObject>>(a)) {
        return std::get<std::shared_ptr<LuaObject>>(a) == std::get<std::shared_ptr<LuaObject>>(b);
    } else if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(a)) {
        return std::get<std::shared_ptr<LuaFunctionWrapper>>(a) == std::get<std::shared_ptr<LuaFunctionWrapper>>(b);
    }
    return false; // Should not reach here
}

// select
std::vector<LuaValue> lua_select(std::shared_ptr<LuaObject> args) {
    LuaValue index_val = args->get("1");
    if (std::holds_alternative<std::string>(index_val) && std::get<std::string>(index_val) == "#") {
        // select("#", ...) returns the total number of arguments
        long long count = 0;
        for (int i = 2; ; ++i) {
            if (std::holds_alternative<std::monostate>(args->get(std::to_string(i)))) {
                break;
            }
            count++;
        }
        return {static_cast<double>(count)};
    } else if (std::holds_alternative<double>(index_val)) {
        // select(n, ...) returns arguments from n onwards
        long long n = static_cast<long long>(std::get<double>(index_val));
        std::vector<LuaValue> results_vec;
        for (long long i = n + 1; ; ++i) {
            LuaValue val = args->get(std::to_string(i));
            if (std::holds_alternative<std::monostate>(val)) {
                break;
            }
            results_vec.push_back(val);
        }
        return results_vec;
    }
    return {std::monostate{}}; // Should not happen, return nil
}

// xpcall
std::vector<LuaValue> lua_xpcall(std::shared_ptr<LuaObject> args) {
    LuaValue func_to_call = args->get("1");
    LuaValue error_handler = args->get("2");

    if (!std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(func_to_call)) {
        return {false}; // Not a callable function
    }
    if (!std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(error_handler)) {
        return {false}; // Not a callable error handler
    }

    auto callable_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(func_to_call);
    auto callable_err_handler = std::get<std::shared_ptr<LuaFunctionWrapper>>(error_handler);

    auto func_args = std::make_shared<LuaObject>();
    for (int i = 3; ; ++i) { // Arguments for func_to_call start from index 3
        LuaValue arg = args->get(std::to_string(i));
        if (std::holds_alternative<std::monostate>(arg)) break;
        func_args->set(std::to_string(i - 2), arg);
    }

    try {
        std::vector<LuaValue> results_from_func = callable_func->func(func_args);
        std::vector<LuaValue> results_vec;
        results_vec.push_back(true);
        results_vec.insert(results_vec.end(), results_from_func.begin(), results_from_func.end());
        return results_vec;
    } catch (const std::exception& e) {
        auto err_args = std::make_shared<LuaObject>();
        err_args->set("1", e.what());
        std::vector<LuaValue> handled_error_msg = callable_err_handler->func(err_args);

        std::vector<LuaValue> results_vec;
        results_vec.push_back(false);
        results_vec.insert(results_vec.end(), handled_error_msg.begin(), handled_error_msg.end());
        return results_vec;
    } catch (...) {
        auto err_args = std::make_shared<LuaObject>();
        err_args->set("1", "An unknown error occurred");
        std::vector<LuaValue> handled_error_msg = callable_err_handler->func(err_args);

        std::vector<LuaValue> results_vec;
        results_vec.push_back(false);
        results_vec.insert(results_vec.end(), handled_error_msg.begin(), handled_error_msg.end());
        return results_vec;
    }
}

// warn (prints to stderr)
std::vector<LuaValue> lua_warn(std::shared_ptr<LuaObject> args) {
    for (int i = 1; ; ++i) {
        LuaValue val = args->get(std::to_string(i));
        if (std::holds_alternative<std::monostate>(val)) break;
        std::cerr << "Lua warning: " << to_cpp_string(val) << std::endl;
    }
    return {std::monostate{}};
}

// rawlen
std::vector<LuaValue> lua_rawlen(std::shared_ptr<LuaObject> args) {
    LuaValue v = args->get("1");
    if (std::holds_alternative<std::string>(v)) {
        return {static_cast<double>(std::get<std::string>(v).length())};
    } else if (std::holds_alternative<std::shared_ptr<LuaObject>>(v)) {
        auto obj = std::get<std::shared_ptr<LuaObject>>(v);
        // For tables, rawlen returns the size of the sequence part
        long long count = 0;
        for (long long i = 1; ; ++i) {
            if (obj->properties.count(std::to_string(i))) {
                count++;
            } else {
                break;
            }
        }
        return {static_cast<double>(count)};
    }
    return {0.0}; // For other types, rawlen is 0
}

// rawequal
std::vector<LuaValue> lua_rawequal(std::shared_ptr<LuaObject> args) {
    LuaValue a = args->get("1");
    LuaValue b = args->get("2");
    // Raw equality bypasses metamethods and type conversions.
    // It's essentially a direct comparison of the underlying C++ types.
    // For LuaObjects and LuaFunctionWrappers, it's pointer equality.
    // For other types, it's value equality.
    if (a.index() != b.index()) {
        return {false};
    }
    if (std::holds_alternative<std::monostate>(a)) {
        return {true};
    } else if (std::holds_alternative<bool>(a)) {
        return {std::get<bool>(a) == std::get<bool>(b)};
    } else if (std::holds_alternative<double>(a)) {
        return {std::get<double>(a) == std::get<double>(b)};
    } else if (std::holds_alternative<std::string>(a)) {
        return {std::get<std::string>(a) == std::get<std::string>(b)};
    } else if (std::holds_alternative<std::shared_ptr<LuaObject>>(a)) {
        return {std::get<std::shared_ptr<LuaObject>>(a) == std::get<std::shared_ptr<LuaObject>>(b)};
    } else if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(a)) {
        return {std::get<std::shared_ptr<LuaFunctionWrapper>>(a) == std::get<std::shared_ptr<LuaFunctionWrapper>>(b)};
    }
    return {false};
}

// next (not supported in translated environment)
std::vector<LuaValue> lua_next(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("next is not supported in the translated environment.");
    return {}; // Should not be reached
}

// load (not supported in translated environment)
std::vector<LuaValue> lua_load(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("load is not supported in the translated environment.");
    return {}; // Should not be reached
}

// loadfile (not supported in translated environment)
std::vector<LuaValue> lua_loadfile(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("loadfile is not supported in the translated environment.");
    return {}; // Should not be reached
}



// dofile (not supported in translated environment)
std::vector<LuaValue> lua_dofile(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("dofile is not supported in the translated environment.");
    return {}; // Should not be reached
}

// collectgarbage (no-op for now)
std::vector<LuaValue> lua_collectgarbage(std::shared_ptr<LuaObject> args) {
    return {std::monostate{}};
}

// assert
std::vector<LuaValue> lua_assert(std::shared_ptr<LuaObject> args) {
    LuaValue condition = args->get("1");
    if (!std::holds_alternative<bool>(condition) || !std::get<bool>(condition)) {
        LuaValue message = args->get("2");
        if (std::holds_alternative<std::monostate>(message)) {
            throw std::runtime_error("assertion failed!");
        } else {
            throw std::runtime_error(to_cpp_string(message));
        }
    }
    // Return all arguments after the condition
    std::vector<LuaValue> results_vec;
    for (int i = 2; ; ++i) {
        LuaValue val = args->get(std::to_string(i));
        if (std::holds_alternative<std::monostate>(val)) break;
        results_vec.push_back(val);
    }
    return results_vec;
}

// Helper for Lua-style inequality comparison
bool lua_not_equals(const LuaValue& a, const LuaValue& b) {
    return !lua_equals(a, b);
}

bool is_lua_truthy(const LuaValue& val) {
    if (std::holds_alternative<std::monostate>(val) || (std::holds_alternative<bool>(val) && std::get<bool>(val) == false)) {
        return false;
    }
    return true;
}