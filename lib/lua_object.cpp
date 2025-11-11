#include "lua_object.hpp"
#include <iostream>
#include <variant>

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
            std::get<std::shared_ptr<LuaFunctionWrapper>>(newindex)->func(shared_from_this(), key, value);
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
        return std::to_string(std::get<double>(value));
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

bool operator<=(const LuaValue& lhs, const LuaValue& rhs) {
    if (std::holds_alternative<double>(lhs) && std::holds_alternative<double>(rhs)) {
        return std::get<double>(lhs) <= std::get<double>(rhs);
    }
    return false;
}
