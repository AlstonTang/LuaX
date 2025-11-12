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
    }
    return 0.0;
}

std::string to_cpp_string(const LuaValue& value) {
    if (std::holds_alternative<double>(value)) {
        double d = std::get<double>(value);
        if (d == static_cast<long long>(d)) {
            return std::to_string(static_cast<long long>(d));
        } else {
            return std::to_string(d);
        }
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

// Helper for Lua-style inequality comparison
bool lua_not_equals(const LuaValue& a, const LuaValue& b) {
    return !lua_equals(a, b);
}
