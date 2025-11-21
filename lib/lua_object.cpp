#include "lua_object.hpp"
#include <iostream>
#include <variant>
#include <algorithm> // for std::sort

std::shared_ptr<LuaObject> _G = std::make_shared<LuaObject>();

LuaValue LuaObject::get(const std::string& key) {
    return get_item(key);
}

void LuaObject::set(const std::string& key, const LuaValue& value) {
    set_item(key, value);
}

void LuaObject::set_metatable(std::shared_ptr<LuaObject> mt) {
    metatable = mt;
}

LuaValue LuaObject::get_item(const LuaValue& key) {
    // Try to access as integer key first
    if (std::holds_alternative<long long>(key)) {
        long long int_key = std::get<long long>(key);
        if (array_properties.count(int_key)) {
            return array_properties[int_key];
        }
    } else if (std::holds_alternative<double>(key)) {
        double double_key = std::get<double>(key);
        if (double_key == static_cast<long long>(double_key)) { // Check if it's an integer
            long long int_key = static_cast<long long>(double_key);
            if (array_properties.count(int_key)) {
                return array_properties[int_key];
            }
        }
    }

    // Then try to access as string key
    std::string key_str = to_cpp_string(key);
    if (properties.count(key_str)) {
        return properties[key_str];
    }

    // Metatable __index logic
    if (metatable) {
        auto index = metatable->get_item("__index"); // Use get_item for metatable access
        if (std::holds_alternative<std::shared_ptr<LuaObject>>(index)) {
            return std::get<std::shared_ptr<LuaObject>>(index)->get_item(key);
        } else if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(index)) {
            auto args = std::make_shared<LuaObject>();
            args->set_item("1", shared_from_this());
            args->set_item("2", key);
            std::vector<LuaValue> results = std::get<std::shared_ptr<LuaFunctionWrapper>>(index)->func(args);
            if (!results.empty()) {
                return results[0];
            }
        }
    }
    return std::monostate{}; // nil
}

void LuaObject::set_item(const LuaValue& key, const LuaValue& value) {
    // Metatable __newindex logic
    if (metatable) {
        auto newindex = metatable->get_item("__newindex"); // Use get_item for metatable access
        if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(newindex)) {
            auto args = std::make_shared<LuaObject>();
            args->set_item("1", shared_from_this());
            args->set_item("2", key);
            args->set_item("3", value);
            std::get<std::shared_ptr<LuaFunctionWrapper>>(newindex)->func(args);
            return;
        } else if (std::holds_alternative<std::shared_ptr<LuaObject>>(newindex)) {
            std::get<std::shared_ptr<LuaObject>>(newindex)->set_item(key, value);
            return;
        }
    }

    // Try to set as integer key first
    if (std::holds_alternative<long long>(key)) {
        long long int_key = std::get<long long>(key);
        array_properties[int_key] = value;
        return;
    } else if (std::holds_alternative<double>(key)) {
        double double_key = std::get<double>(key);
        if (double_key == static_cast<long long>(double_key)) { // Check if it's an integer
            long long int_key = static_cast<long long>(double_key);
            array_properties[int_key] = value;
            return;
        }
    }

    // Then set as string key
    std::string key_str = to_cpp_string(key);
    properties[key_str] = value;
}

void LuaObject::set_item(const LuaValue& key, std::vector<LuaValue> value) {
    LuaObject::set_item(key, value.at(0));
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
    } else if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(value)) {
        std::cout << "<function>";
    } else if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(value)) {
        std::cout << "<thread>";
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
    } else if (std::holds_alternative<double>(value)) {
        return static_cast<long long>(std::get<double>(value));
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
    } else if (std::holds_alternative<long long>(value)) {
        return std::to_string(std::get<long long>(value));
    } else if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    } else if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "true" : "false";
    } else if (std::holds_alternative<std::shared_ptr<LuaObject>>(value)) {
        return "<table>"; // Or a unique identifier for the table
    } else if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(value)) {
        return "<function>"; // Or a unique identifier for the function
    } else if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(value)) {
        return "<thread>"; // Or a unique identifier for the coroutine
    } else {
        return "nil";
    }
}

std::string to_cpp_string(std::vector<LuaValue> value) {
    return to_cpp_string(value.at(0));
}

// rawget
std::vector<LuaValue> lua_rawget(std::shared_ptr<LuaObject> args) {
    auto table_val = args->get_item("1");
    auto key = args->get_item("2");

    auto table = get_object(table_val);
    if (!table) {
        throw std::runtime_error("bad argument #1 to 'rawget' (table expected)");
    }

    // Rawget bypasses metatables, so we access the properties directly.

    // Try to access as integer key first
    if (std::holds_alternative<long long>(key)) {
        long long int_key = std::get<long long>(key);
        if (table->array_properties.count(int_key)) {
            return {table->array_properties.at(int_key)};
        }
    } else if (std::holds_alternative<double>(key)) {
        double double_key = std::get<double>(key);
        if (double_key == static_cast<long long>(double_key)) { // Check if it's an integer
            long long int_key = static_cast<long long>(double_key);
            if (table->array_properties.count(int_key)) {
                return {table->array_properties.at(int_key)};
            }
        }
    }

    // Then try to access as string key
    std::string key_str = to_cpp_string(key);
    if (table->properties.count(key_str)) {
        return {table->properties.at(key_str)};
    }

    return {std::monostate{}}; // Return nil
}

// rawset
std::vector<LuaValue> lua_rawset(std::shared_ptr<LuaObject> args) {
    auto table_val = args->get_item("1");
    auto key = args->get_item("2");
    auto value = args->get_item("3");

    auto table = get_object(table_val);
    if (!table) {
        throw std::runtime_error("bad argument #1 to 'rawset' (table expected)");
    }

    // Rawset bypasses metatables, so we set the properties directly.

    // Try to set as integer key first
    if (std::holds_alternative<long long>(key)) {
        long long int_key = std::get<long long>(key);
        table->array_properties[int_key] = value;
    } else if (std::holds_alternative<double>(key)) {
        double double_key = std::get<double>(key);
        if (double_key == static_cast<long long>(double_key)) { // Check if it's an integer
            long long int_key = static_cast<long long>(double_key);
            table->array_properties[int_key] = value;
        } else {
            // Non-integer doubles become string keys
            std::string key_str = to_cpp_string(key);
            table->properties[key_str] = value;
        }
    } else {
        // All other types become string keys
        std::string key_str = to_cpp_string(key);
        table->properties[key_str] = value;
    }

    return {table_val}; // Return the table
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
        if (std::holds_alternative<long long>(a) && std::holds_alternative<std::string>(b)) {
            try { return std::get<long long>(a) == std::stoll(std::get<std::string>(b)); } catch (...) { return false; }
        }
        if (std::holds_alternative<std::string>(a) && std::holds_alternative<long long>(b)) {
            try { return std::stoll(std::get<std::string>(a)) == std::get<long long>(b); } catch (...) { return false; }
        }
        if (std::holds_alternative<double>(a) && std::holds_alternative<long long>(b)) {
            return std::get<double>(a) == static_cast<double>(std::get<long long>(b));
        }
        if (std::holds_alternative<long long>(a) && std::holds_alternative<double>(b)) {
            return static_cast<double>(std::get<long long>(a)) == std::get<double>(b);
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
    } else if (std::holds_alternative<long long>(a)) {
        return std::get<long long>(a) == std::get<long long>(b);
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
    LuaValue index_val = args->get_item("1");
    if (std::holds_alternative<std::string>(index_val) && std::get<std::string>(index_val) == "#") {
        // select("#", ...) returns the total number of arguments
        long long count = 0;
        for (int i = 2; ; ++i) {
            if (std::holds_alternative<std::monostate>(args->get_item(std::to_string(i)))) {
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
            LuaValue val = args->get_item(std::to_string(i));
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
    LuaValue func_to_call = args->get_item("1");
    LuaValue error_handler = args->get_item("2");

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
        LuaValue arg = args->get_item(std::to_string(i));
        if (std::holds_alternative<std::monostate>(arg)) break;
        func_args->set_item(std::to_string(i - 2), arg);
    }

    try {
        std::vector<LuaValue> results_from_func = callable_func->func(func_args);
        std::vector<LuaValue> results_vec;
        results_vec.push_back(true);
        results_vec.insert(results_vec.end(), results_from_func.begin(), results_from_func.end());
        return results_vec;
    } catch (const std::exception& e) {
        auto err_args = std::make_shared<LuaObject>();
        err_args->set_item("1", std::string(e.what()));
        std::vector<LuaValue> handled_error_msg = callable_err_handler->func(err_args);

        std::vector<LuaValue> results_vec;
        results_vec.push_back(false);
        results_vec.insert(results_vec.end(), handled_error_msg.begin(), handled_error_msg.end());
        return results_vec;
    } catch (...) {
        auto err_args = std::make_shared<LuaObject>();
        err_args->set_item("1", std::string("An unknown error occurred"));
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
        LuaValue val = args->get_item(std::to_string(i));
        if (std::holds_alternative<std::monostate>(val)) break;
        std::cerr << "Lua warning: " << to_cpp_string(val) << std::endl;
    }
    return {std::monostate{}};
}

// rawlen
std::vector<LuaValue> lua_rawlen(std::shared_ptr<LuaObject> args) {
    LuaValue v = args->get_item("1");
    if (std::holds_alternative<std::string>(v)) {
        return {static_cast<double>(std::get<std::string>(v).length())};
    } else if (std::holds_alternative<std::shared_ptr<LuaObject>>(v)) {
        auto obj = std::get<std::shared_ptr<LuaObject>>(v);
        // For tables, rawlen returns the size of the sequence part
        long long count = 0;
        for (long long i = 1; ; ++i) {
            if (obj->array_properties.count(i)) {
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
    LuaValue a = args->get_item("1");
    LuaValue b = args->get_item("2");
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

// next (iterator for tables)
std::vector<LuaValue> lua_next(std::shared_ptr<LuaObject> args) {
    auto table = get_object(args->get_item("1"));
    if (!table) {
        throw std::runtime_error("bad argument #1 to 'next' (table expected)");
    }
    LuaValue key = args->get_item("2");

    // Combine keys from both array_properties and properties for iteration
    std::vector<LuaValue> all_keys;
    for (auto const& [k, v] : table->array_properties) {
        all_keys.push_back(k);
    }
    for (auto const& [k, v] : table->properties) {
        all_keys.push_back(k);
    }

    // Sort keys to ensure consistent iteration order (Lua's next has an unspecified order for non-integer keys)
    // For simplicity, we'll sort by string representation.
    std::sort(all_keys.begin(), all_keys.end(), [](const LuaValue& a, const LuaValue& b) {
        // Custom comparison logic for LuaValue
        // Prioritize numerical comparison for numbers
        if (std::holds_alternative<long long>(a) && std::holds_alternative<long long>(b)) {
            return std::get<long long>(a) < std::get<long long>(b);
        }
        if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
            return std::get<double>(a) < std::get<double>(b);
        }
        if (std::holds_alternative<long long>(a) && std::holds_alternative<double>(b)) {
            return static_cast<double>(std::get<long long>(a)) < std::get<double>(b);
        }
        if (std::holds_alternative<double>(a) && std::holds_alternative<long long>(b)) {
            return std::get<double>(a) < static_cast<double>(std::get<long long>(b));
        }

        // Fallback to string comparison for other types or mixed types
        return to_cpp_string(a) < to_cpp_string(b);
    });

    if (std::holds_alternative<std::monostate>(key)) { // key is nil, return first element
        if (!all_keys.empty()) {
            LuaValue first_key = all_keys[0];
            return {first_key, table->get_item(first_key)};
        }
    } else { // key is not nil, return next element
        bool found_current_key = false;
        for (size_t i = 0; i < all_keys.size(); ++i) {
            if (lua_equals(all_keys[i], key)) {
                found_current_key = true;
                if (i + 1 < all_keys.size()) {
                    LuaValue next_key = all_keys[i+1];
                    return {next_key, table->get_item(next_key)};
                }
                break;
            }
        }
    }
    return {std::monostate{}}; // No more elements
}

// ipairs iterator function
std::vector<LuaValue> ipairs_iterator(std::shared_ptr<LuaObject> args) {
    auto table = get_object(args->get_item("1"));
    if (!table) {
        // This should not happen if used correctly, but it's good practice to check.
        throw std::runtime_error("ipairs iterator called with non-table state");
    }
    long long index = get_long_long(args->get_item("2"));

    long long next_index = index + 1;
    // Use get_item to respect potential __index metamethods on the array part
    LuaValue next_value = table->get_item(static_cast<double>(next_index));

    if (std::holds_alternative<std::monostate>(next_value)) {
        return {}; // End of iteration, return nil
    }

    return {static_cast<double>(next_index), next_value};
}

// ipairs
std::vector<LuaValue> lua_ipairs(std::shared_ptr<LuaObject> args) {
    auto table = get_object(args->get_item("1"));
    if (!table) {
        throw std::runtime_error("bad argument #1 to 'ipairs' (table expected)");
    }

    // Check for __ipairs metamethod
    if (table->metatable) {
        auto ipairs_meta = table->metatable->get_item("__ipairs");
        if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(ipairs_meta)) {
            auto meta_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(ipairs_meta);
            auto meta_args = std::make_shared<LuaObject>();
            meta_args->set_item("1", table);
            return meta_func->func(meta_args);
        }
    }

    // Return the standard iterator function, the table, and initial index 0
    auto iterator_func_wrapper = std::make_shared<LuaFunctionWrapper>(ipairs_iterator);
    return {iterator_func_wrapper, table, 0.0};
}

// pairs
std::vector<LuaValue> lua_pairs(std::shared_ptr<LuaObject> args) {
    auto table = get_object(args->get_item("1"));
    if (!table) {
        throw std::runtime_error("bad argument #1 to 'pairs' (table expected)");
    }

    // Check for __pairs metamethod
    if (table->metatable) {
        auto pairs_meta = table->metatable->get_item("__pairs");
        if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(pairs_meta)) {
            auto meta_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(pairs_meta);
            auto meta_args = std::make_shared<LuaObject>();
            meta_args->set_item("1", table);
            return meta_func->func(meta_args);
        }
    }

    // Default behavior: return next, the table, and nil
    auto next_func_wrapper = std::make_shared<LuaFunctionWrapper>(lua_next);
    return {next_func_wrapper, table, std::monostate{}};
}


// load (not supported in translated environment)
std::vector<LuaValue> lua_load(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("load is not supported in the translated environment.");
    return {std::monostate()}; // Should not be reached
}

// loadfile (not supported in translated environment)
std::vector<LuaValue> lua_loadfile(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("loadfile is not supported in the translated environment. Tip: Use 'require' instead and ensure that dependencies are statically resolvable.");
    return {std::monostate()}; // Should not be reached
}

// dofile (not supported in translated environment)
std::vector<LuaValue> lua_dofile(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("dofile is not supported in the translated environment. Tip: Use 'require' instead and ensure that dependencies are statically resolvable.");
    return {std::monostate()}; // Should not be reached
}

// collectgarbage (no-op for now)
std::vector<LuaValue> lua_collectgarbage(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("Collect Garbage is not support in the translated environment. Since the IR is C++, there is no such thing as garbage collection here!");
    return {std::monostate{}};
}

// assert
std::vector<LuaValue> lua_assert(std::shared_ptr<LuaObject> args) {
    LuaValue condition = args->get_item("1");
    if (!std::holds_alternative<bool>(condition) || !std::get<bool>(condition)) {
        LuaValue message = args->get_item("2");
        if (std::holds_alternative<std::monostate>(message)) {
            throw std::runtime_error("ERROR: assertion failed!");
        } else {
            throw std::runtime_error(to_cpp_string(message));
        }
    }
    // Return all arguments after the condition
    std::vector<LuaValue> results_vec;
    for (int i = 2; ; ++i) {
        LuaValue val = args->get_item(std::to_string(i));
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

// Helper to get the string name of a LuaValue's type for error messages
std::string get_lua_type_name(const LuaValue& val) {
    if (std::holds_alternative<std::monostate>(val)) return "nil";
    if (std::holds_alternative<bool>(val)) return "boolean";
    if (std::holds_alternative<double>(val) || std::holds_alternative<long long>(val)) return "number";
    if (std::holds_alternative<std::string>(val)) return "string";
    if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) return "table";
    if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(val)) return "function";
    if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(val)) return "thread";
    return "unknown";
}


// Safely calls a LuaValue, checking for callability and handling the __call metamethod.
std::vector<LuaValue> call_lua_value(const LuaValue& callable, std::shared_ptr<LuaObject> args) {
    // Case 1: The value is a direct function.
    if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(callable)) {
        auto func_wrapper = std::get<std::shared_ptr<LuaFunctionWrapper>>(callable);
        return func_wrapper->func(args);
    }

    // Case 2: The value is a table or userdata; check for a __call metamethod.
    if (std::holds_alternative<std::shared_ptr<LuaObject>>(callable)) {
        auto table_obj = std::get<std::shared_ptr<LuaObject>>(callable);
        if (table_obj->metatable) {
            LuaValue call_meta = table_obj->metatable->get_item("__call");
            if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(call_meta)) {
                // The metamethod exists and is a function.
                auto meta_func_wrapper = std::get<std::shared_ptr<LuaFunctionWrapper>>(call_meta);

                // Create new arguments: the first argument is the table itself,
                // followed by the original arguments.
                auto new_args = std::make_shared<LuaObject>();
                new_args->set_item("1", table_obj); // 'self' is the table being called

                // Copy original arguments, shifting their indices by 1.
                for (long long i = 1; ; ++i) {
                    LuaValue original_arg = args->get_item(std::to_string(i));
                    if (std::holds_alternative<std::monostate>(original_arg)) {
                        break; // No more arguments
                    }
                    new_args->set_item(std::to_string(i + 1), original_arg);
                }

                // Call the metamethod with the new arguments.
                return meta_func_wrapper->func(new_args);
            }
        }
    }

    // Case 3: The value is not callable. Throw a runtime error.
    throw std::runtime_error("attempt to call a " + get_lua_type_name(callable) + " value");
}

LuaValue lua_get_member(const LuaValue& base, const LuaValue& key) {
    if (std::holds_alternative<std::shared_ptr<LuaObject>>(base)) {
        return std::get<std::shared_ptr<LuaObject>>(base)->get_item(key);
    } else if (std::holds_alternative<std::string>(base)) {
        // String library lookup
        auto string_lib = _G->get_item("string");
        if (std::holds_alternative<std::shared_ptr<LuaObject>>(string_lib)) {
            return std::get<std::shared_ptr<LuaObject>>(string_lib)->get_item(key);
        }
    }
    throw std::runtime_error("attempt to index a " + get_lua_type_name(base) + " value");
}

LuaValue lua_get_length(const LuaValue& val) {
    if (std::holds_alternative<std::string>(val)) {
        return static_cast<double>(std::get<std::string>(val).length());
    } else if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) {
        auto obj = std::get<std::shared_ptr<LuaObject>>(val);
        // Check for __len metamethod
        if (obj->metatable) {
            auto len_meta = obj->metatable->get_item("__len");
            if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(len_meta)) {
                auto meta_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(len_meta);
                auto args = std::make_shared<LuaObject>();
                args->set_item("1", obj);
                std::vector<LuaValue> res = meta_func->func(args);
                if (!res.empty()) return res[0];
                return std::monostate{};
            }
        }
        // Default table length: largest integer key
        if (obj->array_properties.empty()) return 0.0;
        return static_cast<double>(obj->array_properties.rbegin()->first);
    }
    throw std::runtime_error("attempt to get length of a " + get_lua_type_name(val) + " value");
}