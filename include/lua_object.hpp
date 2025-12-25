#ifndef LUA_OBJECT_HPP
#define LUA_OBJECT_HPP

#include <string>
#include <utility>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <functional>
#include <stdexcept>
#include "lua_value.hpp"

// Forward declaration
class LuaObject;

// 1. Refactored Wrapper: Returns void, takes Output Parameter
struct LuaFunctionWrapper {
	// Function signature definition
	using FuncSignature = std::function<void(const LuaValue*, size_t, std::vector<LuaValue>&)>;

	FuncSignature func;

	// Default constructor
	LuaFunctionWrapper() = default;

	// Template constructor: Accepts raw function pointers, lambdas, or std::function
	// This fixes the "no matching function" error with std::make_shared
	template <typename F>
	LuaFunctionWrapper(F&& f) : func(std::forward<F>(f)) {}
};

// LuaObject Definition
class LuaObject : public std::enable_shared_from_this<LuaObject> {
public:
	virtual ~LuaObject() = default;
	std::unordered_map<LuaValue, LuaValue> properties;
	std::vector<LuaValue> array_part;
	std::shared_ptr<LuaObject> metatable;

	LuaValue get(const std::string& key);
	void set(const std::string& key, const LuaValue& value);
	LuaValue get_item(const LuaValue& key);
	void set_item(const LuaValue& key, const LuaValue& value);
	void set_item(const LuaValue& key, const std::vector<LuaValue>& value);
	void set_metatable(const std::shared_ptr<LuaObject>& mt);
};

extern std::shared_ptr<LuaObject> _G;

// Global helper functions
void print_value(const LuaValue& value);

inline double get_double(const LuaValue& value) {
	if (const double* val = std::get_if<double>(&value)) {
		return *val;
	}

	if (const long long* val = std::get_if<long long>(&value)) {
		return static_cast<double>(*val);
	}

	if (const std::string* str = std::get_if<std::string>(&value)) {
		if (str->empty()) {
			// Original stod throws on empty, so we fall through to the error
			goto error;
		}

		char* end;
		double result = std::strtod(str->c_str(), &end);

		if (end != str->c_str()) {
			return result;
		}
	}

error:
	throw std::runtime_error("Type error: expected number.");
}

inline long long get_long_long(const LuaValue& value) {
	if (std::holds_alternative<long long>(value)) {
		return std::get<long long>(value);
	}
	else if (std::holds_alternative<double>(value)) {
		return static_cast<long long>(std::get<double>(value));
	}
	else if (std::holds_alternative<std::string>(value)) {
		try {
			return std::stoll(std::get<std::string>(value));
		}
		catch (...) {
			// Fall through
		}
	}
	throw std::runtime_error("Type error: expected integer.");
}

inline std::shared_ptr<LuaObject> get_object(const LuaValue& value) {
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(value)) {
		return std::get<std::shared_ptr<LuaObject>>(value);
	}
	if (std::holds_alternative<std::monostate>(value))
		throw std::runtime_error(
			"Type error: expected table or userdata, got nil.");
	if (std::holds_alternative<double>(value))
		throw std::runtime_error(
			"Type error: expected table or userdata, got number.");
	if (std::holds_alternative<long long>(value))
		throw std::runtime_error(
			"Type error: expected table or userdata, got integer.");
	if (std::holds_alternative<bool>(value))
		throw std::runtime_error(
			"Type error: expected table or userdata, got boolean.");
	if (std::holds_alternative<std::string>(value))
		throw std::runtime_error(
			"Type error: expected table or userdata, got string.");
	throw std::runtime_error("Type error: expected table or userdata, got unknown.");
}

// Helpers
std::string to_cpp_string(const LuaValue& value);
std::string to_cpp_string(const std::vector<LuaValue>& value);
std::string get_lua_type_name(const LuaValue& val);

// Declarations for global Lua functions (Updated Signatures)
void lua_assert(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_collectgarbage(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_dofile(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_ipairs(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_load(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_loadfile(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_next(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_pairs(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_rawequal(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_rawlen(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_rawget(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_rawset(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_select(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_warn(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_xpcall(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_print(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result); // Added for completeness

// Iterators
void pairs_iterator(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void ipairs_iterator(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);
void lua_tonumber(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);

// Core Call Function
void call_lua_value(const LuaValue& callable, const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result);

// Overloads for convenience
inline void call_lua_value(const LuaValue& callable, std::vector<LuaValue>& out_result,
                           const std::vector<LuaValue>& args) {
	call_lua_value(callable, args.data(), args.size(), out_result);
}

// Template for variadic arguments (creating vector on stack)
template <typename... Args>
inline void call_lua_value(const LuaValue& callable, std::vector<LuaValue>& out_result, Args&&... args) {
	if constexpr (sizeof...(args) == 0) {
		call_lua_value(callable, nullptr, 0, out_result);
	}
	else {
		const LuaValue stack_args[] = {LuaValue(std::forward<Args>(args))...};
		call_lua_value(callable, stack_args, sizeof...(args), out_result);
	}
}

LuaValue lua_get_member(const LuaValue& base, const LuaValue& key);
LuaValue lua_get_length(const LuaValue& val); // Returns single value, handles buffer internally

inline bool is_lua_truthy(const LuaValue& val) {
	if (std::holds_alternative<std::monostate>(val)) return false;
	if (std::holds_alternative<bool>(val)) return std::get<bool>(val);
	return true;
}

bool operator<=(const LuaValue& lhs, const LuaValue& rhs);

// Comparison Helpers
bool lua_equals(const LuaValue& a, const LuaValue& b);
bool lua_not_equals(const LuaValue& a, const LuaValue& b);
bool lua_less_than(const LuaValue& a, const LuaValue& b);
bool lua_greater_than(const LuaValue& a, const LuaValue& b);
bool lua_less_equals(const LuaValue& a, const LuaValue& b);
bool lua_greater_equals(const LuaValue& a, const LuaValue& b);

LuaValue lua_concat(const LuaValue& a, const LuaValue& b);

// Transpiler helper to safely extract return values
inline LuaValue get_return_value(std::vector<LuaValue>& results, size_t index) {
	// std::move avoids copying the underlying string/table/heavy data
	return index < results.size() ? std::move(results[index]) : std::monostate{};
}

// Logic operators
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
