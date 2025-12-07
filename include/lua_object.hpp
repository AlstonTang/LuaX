#ifndef LUA_OBJECT_HPP
#define LUA_OBJECT_HPP

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <stdexcept> // Required for std::runtime_error
#include "lua_value.hpp" // Include the new LuaValue header

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
	void set_item(const LuaValue& key, std::vector<LuaValue> value); // New method for LuaValue keys
	void set_metatable(std::shared_ptr<LuaObject> mt);
};

extern std::shared_ptr<LuaObject> _G;

// Global helper functions
void print_value(const LuaValue& value);
inline double get_double(const LuaValue& value) {
	if (std::holds_alternative<double>(value)) {
		return std::get<double>(value);
	} else if (std::holds_alternative<long long>(value)) {
		return static_cast<double>(std::get<long long>(value));
	} else if (std::holds_alternative<std::string>(value)) {
		try {
			return std::stod(std::get<std::string>(value));
		} catch (...) {
			// Fall through to error
		}
	}
	throw std::runtime_error("Type error: expected number.");
}

inline long long get_long_long(const LuaValue& value) {
	if (std::holds_alternative<long long>(value)) {
		return std::get<long long>(value);
	} else if (std::holds_alternative<double>(value)) {
		return static_cast<long long>(std::get<double>(value));
	} else if (std::holds_alternative<std::string>(value)) {
		try {
			return std::stoll(std::get<std::string>(value));
		} catch (...) {
			// Fall through to error
		}
	}
	throw std::runtime_error("Type error: expected integer.");
}

inline std::shared_ptr<LuaObject> get_object(const LuaValue& value) {
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(value)) {
		return std::get<std::shared_ptr<LuaObject>>(value);
	}
	if (std::holds_alternative<std::monostate>(value)) throw std::runtime_error("Type error: expected table or userdata, got nil.");
	if (std::holds_alternative<double>(value)) throw std::runtime_error("Type error: expected table or userdata, got number.");
	if (std::holds_alternative<long long>(value)) throw std::runtime_error("Type error: expected table or userdata, got integer.");
	if (std::holds_alternative<bool>(value)) throw std::runtime_error("Type error: expected table or userdata, got boolean.");
	if (std::holds_alternative<std::string>(value)) throw std::runtime_error("Type error: expected table or userdata, got string.");
	throw std::runtime_error("Type error: expected table or userdata, got unknown.");
}

// Helper to safely get a LuaFile from a LuaValue. Throws on type error.
std::string to_cpp_string(const LuaValue& value);
std::string to_cpp_string(std::vector<LuaValue> value);
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
LuaValue lua_get_member(const LuaValue& base, const LuaValue& key);
LuaValue lua_get_length(const LuaValue& val);

inline bool is_lua_truthy(const LuaValue& val) {
	if (std::holds_alternative<std::monostate>(val)) return false;
	if (std::holds_alternative<bool>(val)) return std::get<bool>(val);
	return true;
}

bool operator<=(const LuaValue& lhs, const LuaValue& rhs);

// Helper for Lua-style comparison
bool lua_equals(const LuaValue& a, const LuaValue& b);
bool lua_not_equals(const LuaValue& a, const LuaValue& b);
bool lua_less_than(const LuaValue& a, const LuaValue& b);
bool lua_greater_than(const LuaValue& a, const LuaValue& b);
bool lua_less_equals(const LuaValue& a, const LuaValue& b);
bool lua_greater_equals(const LuaValue& a, const LuaValue& b);

LuaValue lua_concat(const LuaValue& a, const LuaValue& b);

inline LuaValue get_return_value(std::vector<LuaValue> results, size_t index) {
	if (index < results.size()) return results[index];
	return std::monostate{};
}

#endif // LUA_OBJECT_HPP