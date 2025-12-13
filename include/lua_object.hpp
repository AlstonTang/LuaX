#ifndef LUA_OBJECT_HPP
#define LUA_OBJECT_HPP

#include <string>
#include <utility>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <stdexcept>
#include "lua_value.hpp"

// Now define LuaFunctionWrapper, which can now use LuaValue
struct LuaFunctionWrapper {
	std::function<std::vector<LuaValue>(const LuaValue*, size_t)> func;
	LuaFunctionWrapper(std::function<std::vector<LuaValue>(const LuaValue*, size_t)> f) : func(std::move(f)) {}
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
	void set_item(const LuaValue& key, const std::vector<LuaValue>& value); // New method for LuaValue keys
	void set_metatable(const std::shared_ptr<LuaObject>& mt);
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
std::string to_cpp_string(const std::vector<LuaValue>& value);
LuaValue rawget(std::shared_ptr<LuaObject> table, const LuaValue& key);
void rawset(std::shared_ptr<LuaObject> table, const LuaValue& key, const LuaValue& value); // Declaration for rawset

// Declarations for global Lua functions
std::vector<LuaValue> lua_assert(const LuaValue* args, size_t n_args);
std::vector<LuaValue> lua_collectgarbage(const LuaValue* args, size_t n_args);
std::vector<LuaValue> lua_dofile(const LuaValue* args, size_t n_args);
std::vector<LuaValue> lua_ipairs(const LuaValue* args, size_t n_args);
std::vector<LuaValue> lua_load(const LuaValue* args, size_t n_args);
std::vector<LuaValue> lua_loadfile(const LuaValue* args, size_t n_args);
std::vector<LuaValue> lua_next(const LuaValue* args, size_t n_args);
std::vector<LuaValue> lua_pairs(const LuaValue* args, size_t n_args);
std::vector<LuaValue> lua_rawequal(const LuaValue* args, size_t n_args);
std::vector<LuaValue> lua_rawlen(const LuaValue* args, size_t n_args);
std::vector<LuaValue> lua_rawget(const LuaValue* args, size_t n_args);
std::vector<LuaValue> lua_rawset(const LuaValue* args, size_t n_args);
std::vector<LuaValue> lua_select(const LuaValue* args, size_t n_args);
std::vector<LuaValue> lua_warn(const LuaValue* args, size_t n_args);
std::vector<LuaValue> lua_xpcall(const LuaValue* args, size_t n_args);
std::vector<LuaValue> pairs_iterator(const LuaValue* args, size_t n_args);
std::vector<LuaValue> ipairs_iterator(const LuaValue* args, size_t n_args);
std::vector<LuaValue> call_lua_value(const LuaValue& callable, const LuaValue* args, size_t n_args);

inline std::vector<LuaValue> call_lua_value(const LuaValue& callable, const std::vector<LuaValue>& args) {
	return call_lua_value(callable, args.data(), args.size());
}

inline std::vector<LuaValue> call_lua_value(const LuaValue& callable, std::vector<LuaValue>& args) {
	return call_lua_value(callable, args.data(), args.size());
}

inline std::vector<LuaValue> call_lua_value(const LuaValue& callable, std::vector<LuaValue>&& args) {
	return call_lua_value(callable, args.data(), args.size());
}

template<typename... Args>
std::vector<LuaValue> call_lua_value(const LuaValue& callable, Args&&... args) {
	if constexpr (sizeof...(args) == 0) {
		return call_lua_value(callable, static_cast<const LuaValue*>(nullptr), static_cast<size_t>(0));
	} else {
		const LuaValue stack_args[] = {LuaValue(std::forward<Args>(args))...};
		return call_lua_value(callable, stack_args, sizeof...(args));
	}
}

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

inline LuaValue get_return_value(const std::vector<LuaValue>& results, size_t index) {
	if (index < results.size()) return results[index];
	return std::monostate{};
}

inline LuaValue get_return_value(std::vector<LuaValue>&& results, size_t index) {
	if (index < results.size()) return std::move(results[index]);
	return std::monostate{};
}


template <typename T, typename F>
LuaValue lua_logical_or(T&& left, F&& right_provider) {
	if (is_lua_truthy(left)) {
		return LuaValue(std::forward<T>(left));
	}
	return LuaValue(right_provider());
}

template <typename T, typename F>
LuaValue lua_logical_and(T&& left, F&& right_provider) {
	if (!is_lua_truthy(left)) {
		return LuaValue(std::forward<T>(left));
	}
	return LuaValue(right_provider());
}

#endif // LUA_OBJECT_HPP