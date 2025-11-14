#ifndef LUA_OBJECT_HPP
#define LUA_OBJECT_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <variant>
#include <functional>

// Forward declarations
class LuaObject;
struct LuaFunctionWrapper;

// Define LuaValue first, using forward declarations for recursive types
using LuaValue = std::variant<
    std::monostate, // for nil
    bool,
    double,
    long long,
    std::string,
    std::shared_ptr<LuaObject>, // Breaks recursion for LuaObject
    std::shared_ptr<LuaFunctionWrapper> // Breaks recursion for functions
>;

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
    std::shared_ptr<LuaObject> metatable;

    LuaValue get(const std::string& key);
    void set(const std::string& key, const LuaValue& value);
    void set_metatable(std::shared_ptr<LuaObject> mt);
};

extern std::shared_ptr<LuaObject> _G;

// Global helper functions
std::shared_ptr<LuaObject> get_object(const LuaValue& value);
void print_value(const LuaValue& value);
double get_double(const LuaValue& value);
long long get_long_long(const LuaValue& value);
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
std::vector<LuaValue> lua_select(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_warn(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_xpcall(std::shared_ptr<LuaObject> args);
bool is_lua_truthy(const LuaValue& val);

bool operator<=(const LuaValue& lhs, const LuaValue& rhs);

// Helper for Lua-style equality comparison
bool lua_equals(const LuaValue& a, const LuaValue& b);

// Helper for Lua-style inequality comparison
bool lua_not_equals(const LuaValue& a, const LuaValue& b);

#endif // LUA_OBJECT_HPP