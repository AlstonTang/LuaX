#ifndef LUA_OBJECT_HPP
#define LUA_OBJECT_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <stdexcept> // Required for std::runtime_error
#include "lua_value.hpp" // Include the new LuaValue header

// Forward declarations (already in lua_value.hpp, but good to have here for clarity if needed)
// class LuaObject; // Already forward declared in lua_value.hpp
// struct LuaFunctionWrapper; // Already forward declared in lua_value.hpp
// class LuaCoroutine; // Already forward declared in lua_value.hpp

// Now define LuaFunctionWrapper, which can now use LuaValue
struct LuaFunctionWrapper {
    std::function<std::vector<LuaValue>(std::shared_ptr<LuaObject>)> func;
    LuaFunctionWrapper(std::function<std::vector<LuaValue>(std::shared_ptr<LuaObject>)> f) : func(f) {}
};

// Now define LuaObject, which can now use LuaValue
class LuaObject : public std::enable_shared_from_this<LuaObject> {
public:
    virtual ~LuaObject() = default; // Make LuaObject polymorphic
    std::map<std::string, LuaValue> properties;
    std::map<long long, LuaValue> array_properties; // For integer-indexed tables
    std::shared_ptr<LuaObject> metatable;

    LuaValue get(const std::string& key);
    void set(const std::string& key, const LuaValue& value);
    LuaValue get_item(const LuaValue& key); // New method for LuaValue keys
    void set_item(const LuaValue& key, const LuaValue& value); // New method for LuaValue keys
    void set_metatable(std::shared_ptr<LuaObject> mt);
};

extern std::shared_ptr<LuaObject> _G;

// Global helper functions
std::shared_ptr<LuaObject> get_object(const LuaValue& value);
void print_value(const LuaValue& value);
double get_double(const LuaValue& value);
long long get_long_long(const LuaValue& value);
inline std::shared_ptr<LuaObject> get_object(const LuaValue& value) {
    if (std::holds_alternative<std::shared_ptr<LuaObject>>(value)) {
        return std::get<std::shared_ptr<LuaObject>>(value);
    }
    throw std::runtime_error("Type error: expected table or userdata.");
}

// Helper to safely get a LuaFile from a LuaValue. Throws on type error.
std::string to_cpp_string(const LuaValue& value);
LuaValue rawget(std::shared_ptr<LuaObject> table, const LuaValue& key);
void rawset(std::shared_ptr<LuaObject> table, const LuaValue& key, const LuaValue& value); // Declaration for rawset

// Declarations for global Lua functions
std::vector<LuaValue> lua_assert(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_collectgarbage(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_dofile(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_ipairs(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_load(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_loadfile(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_next(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_pairs(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_rawequal(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_rawlen(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_rawget(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_rawset(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_select(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_warn(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_xpcall(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> pairs_iterator(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> ipairs_iterator(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> call_lua_value(const LuaValue& callable, std::shared_ptr<LuaObject> args);

bool is_lua_truthy(const LuaValue& val);

bool operator<=(const LuaValue& lhs, const LuaValue& rhs);

// Helper for Lua-style equality comparison
bool lua_equals(const LuaValue& a, const LuaValue& b);

// Helper for Lua-style inequality comparison
bool lua_not_equals(const LuaValue& a, const LuaValue& b);

#endif // LUA_OBJECT_HPP