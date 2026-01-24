#ifndef LUA_OBJECT_HPP
#define LUA_OBJECT_HPP

#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <stdexcept>
#include "lua_value.hpp"
#include "pool_allocator.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>

// Forward declaration
class LuaObject;
// Virtual Base Class for all callable entities (Functions, Closures, C++ built-ins)
struct LuaCallable {
	virtual ~LuaCallable() = default;
	
	// Core signature for variadic/multi-return calls
	virtual void call(const LuaValue* args, size_t n_args, LuaValueVector& out_result) = 0;

	// Optimization: Direct overloads for single-return calls
	virtual LuaValue call0();
	virtual LuaValue call1(const LuaValue& a1);
	virtual LuaValue call2(const LuaValue& a1, const LuaValue& a2);
	virtual LuaValue call3(const LuaValue& a1, const LuaValue& a2, const LuaValue& a3);
};

// Compatible wrapper for std::function (used by library functions and generic lambdas)
struct LuaFunctionWrapper : public LuaCallable {
	using FuncSignature = std::function<void(const LuaValue*, size_t, LuaValueVector&)>;
	FuncSignature func;

	LuaFunctionWrapper() = default;

	template <typename F>
	LuaFunctionWrapper(F&& f) : func(std::forward<F>(f)) {}

	void call(const LuaValue* args, size_t n_args, LuaValueVector& out_result) override {
		func(args, n_args, out_result);
	}
};

// Generic template to wrap ANY lambda/callable without std::function overhead
template <typename F>
struct LuaLambdaCallable : public LuaCallable {
	F func;
	LuaLambdaCallable(F&& f) : func(std::forward<F>(f)) {}

	void call(const LuaValue* args, size_t n_args, LuaValueVector& out_result) override {
		func(args, n_args, out_result);
	}
};

template <typename F>
inline std::shared_ptr<LuaCallable> make_lua_callable(F&& f) {
	return std::allocate_shared<LuaLambdaCallable<F>>(PoolAllocator<LuaLambdaCallable<F>>{}, std::forward<F>(f));
}

// Specialized templates for functions with known arity to provide direct overrides
template <size_t Arity, typename FVar, typename... FExt>
struct LuaSpecializedCallable;

template <typename FVar, typename F0>
struct LuaSpecializedCallable<0, FVar, F0> : public LuaCallable {
	FVar f_var; F0 f0;
	LuaSpecializedCallable(FVar&& v, F0&& s) : f_var(std::forward<FVar>(v)), f0(std::forward<F0>(s)) {}
	void call(const LuaValue* args, size_t n, LuaValueVector& out) override { f_var(args, n, out); }
	LuaValue call0() override { return f0(); }
};

template <typename FVar, typename F1>
struct LuaSpecializedCallable<1, FVar, F1> : public LuaCallable {
	FVar f_var; F1 f1;
	LuaSpecializedCallable(FVar&& v, F1&& s) : f_var(std::forward<FVar>(v)), f1(std::forward<F1>(s)) {}
	void call(const LuaValue* args, size_t n, LuaValueVector& out) override { f_var(args, n, out); }
	LuaValue call1(const LuaValue& a1) override { return f1(a1); }
};

template <typename FVar, typename F2>
struct LuaSpecializedCallable<2, FVar, F2> : public LuaCallable {
	FVar f_var; F2 f2;
	LuaSpecializedCallable(FVar&& v, F2&& s) : f_var(std::forward<FVar>(v)), f2(std::forward<F2>(s)) {}
	void call(const LuaValue* args, size_t n, LuaValueVector& out) override { f_var(args, n, out); }
	LuaValue call2(const LuaValue& a1, const LuaValue& a2) override { return f2(a1, a2); }
};

template <typename FVar, typename F3>
struct LuaSpecializedCallable<3, FVar, F3> : public LuaCallable {
	FVar f_var; F3 f3;
	LuaSpecializedCallable(FVar&& v, F3&& s) : f_var(std::forward<FVar>(v)), f3(std::forward<F3>(s)) {}
	void call(const LuaValue* args, size_t n, LuaValueVector& out) override { f_var(args, n, out); }
	LuaValue call3(const LuaValue& a1, const LuaValue& a2, const LuaValue& a3) override { return f3(a1, a2, a3); }
};

template <size_t Arity, typename FVar, typename FSpec>
inline std::shared_ptr<LuaCallable> make_specialized_callable(FVar&& v, FSpec&& s) {
	return std::allocate_shared<LuaSpecializedCallable<Arity, FVar, FSpec>>(PoolAllocator<LuaSpecializedCallable<Arity, FVar, FSpec>>{}, std::forward<FVar>(v), std::forward<FSpec>(s));
}

// LuaObject Definition
class LuaObject : public std::enable_shared_from_this<LuaObject> {
public:
	virtual ~LuaObject() = default;
	
	// Hybrid storage: small vector for few properties, map for many.
	// This drastically improves performance for tokens and AST nodes.
	using PropPair = std::pair<LuaValue, LuaValue>;
	std::vector<PropPair, PoolAllocator<PropPair>> small_props;
	using PropMap = std::unordered_map<LuaValue, LuaValue, LuaValueHash, LuaValueEq, PoolAllocator<std::pair<const LuaValue, LuaValue>>>;
	std::unique_ptr<PropMap> properties;
	
	std::vector<LuaValue, PoolAllocator<LuaValue>> array_part;
	std::shared_ptr<LuaObject> metatable;
	
	static const size_t SMALL_TABLE_THRESHOLD = 16;

	static LuaValue intern_key(const LuaValue& v) {
		if (v.index() == INDEX_STRING) return intern(std::get<std::string>(v));
		if (v.index() == INDEX_STRING_VIEW) return intern(std::get<std::string_view>(v));
		return v;
	}

	static std::shared_ptr<LuaObject> create(
		std::initializer_list<std::pair<LuaValue, LuaValue>> props = {},
		std::initializer_list<LuaValue> arr = {}
	) {
		auto obj = std::allocate_shared<LuaObject>(PoolAllocator<LuaObject>{});
		if (props.size() > SMALL_TABLE_THRESHOLD) {
			obj->properties = std::make_unique<PropMap>();
            for (auto& p : props) (*obj->properties)[intern_key(p.first)] = p.second;
		} else {
			obj->small_props.reserve(props.size());
			for (auto& p : props) obj->small_props.push_back({intern_key(p.first), p.second});
		}
		obj->array_part = arr;
		return obj;
	}

	static std::shared_ptr<LuaObject> create(std::pair<LuaValue, LuaValue> p1) {
		auto obj = std::allocate_shared<LuaObject>(PoolAllocator<LuaObject>{});
		obj->small_props.reserve(1);
		obj->small_props.push_back({intern_key(p1.first), std::move(p1.second)});
		return obj;
	}

	static std::shared_ptr<LuaObject> create(std::pair<LuaValue, LuaValue> p1, std::pair<LuaValue, LuaValue> p2) {
		auto obj = std::allocate_shared<LuaObject>(PoolAllocator<LuaObject>{});
		obj->small_props.reserve(2);
		obj->small_props.push_back({intern_key(p1.first), std::move(p1.second)});
		obj->small_props.push_back({intern_key(p2.first), std::move(p2.second)});
		return obj;
	}

	static std::shared_ptr<LuaObject> create(std::pair<LuaValue, LuaValue> p1, std::pair<LuaValue, LuaValue> p2, std::pair<LuaValue, LuaValue> p3) {
		auto obj = std::allocate_shared<LuaObject>(PoolAllocator<LuaObject>{});
		obj->small_props.reserve(3);
		obj->small_props.push_back({intern_key(p1.first), std::move(p1.second)});
		obj->small_props.push_back({intern_key(p2.first), std::move(p2.second)});
		obj->small_props.push_back({intern_key(p3.first), std::move(p3.second)});
		return obj;
	}

	static std::shared_ptr<LuaObject> create(std::pair<LuaValue, LuaValue> p1, std::pair<LuaValue, LuaValue> p2, std::pair<LuaValue, LuaValue> p3, std::pair<LuaValue, LuaValue> p4) {
		auto obj = std::allocate_shared<LuaObject>(PoolAllocator<LuaObject>{});
		obj->small_props.reserve(4);
		obj->small_props.push_back({intern_key(p1.first), std::move(p1.second)});
		obj->small_props.push_back({intern_key(p2.first), std::move(p2.second)});
		obj->small_props.push_back({intern_key(p3.first), std::move(p3.second)});
		obj->small_props.push_back({intern_key(p4.first), std::move(p4.second)});
		return obj;
	}

	LuaValue get(std::string_view key);
	LuaValue get(const std::string& key) { return get(std::string_view(key)); }
	LuaValue get(const char* key) { return get(std::string_view(key)); }
	// Overload for LuaValue key (extracts string from variant)
	inline LuaValue get(const LuaValue& key) {
		auto idx = key.index();
		if (idx == INDEX_STRING) {
			return get(std::string_view(std::get<INDEX_STRING>(key)));
		}
		if (idx == INDEX_STRING_VIEW) {
			return get(std::get<INDEX_STRING_VIEW>(key));
		}
		return get_item(key);
	}

	void set(std::string_view key, const LuaValue& value);
	void set(const std::string& key, const LuaValue& value) { set(std::string_view(key), value); }
	void set(const char* key, const LuaValue& value) { set(std::string_view(key), value); }
	// Overload for LuaValue key (extracts string from variant)
	inline void set(const LuaValue& key, const LuaValue& value) {
		auto idx = key.index();
		if (idx == INDEX_STRING) {
			set(std::string_view(std::get<INDEX_STRING>(key)), value);
		} else if (idx == INDEX_STRING_VIEW) {
			set(std::get<INDEX_STRING_VIEW>(key), value);
		} else {
			set_item(key, value);
		}
	}

	LuaValue get_item(const LuaValue& key);
	LuaValue get_item(std::string_view key);
	LuaValue get_item(long long key);
	LuaValue get_item(const std::string& key) { return get_item(std::string_view(key)); }
	LuaValue get_item(const char* key) { return get_item(std::string_view(key)); }

	void set_item(const LuaValue& key, const LuaValue& value);
	void set_item(std::string_view key, const LuaValue& value);
	void set_item(long long key, const LuaValue& value);
	void set_item(const std::string& key, const LuaValue& value) { set_item(std::string_view(key), value); }
	void set_item(const char* key, const LuaValue& value) { set_item(std::string_view(key), value); }
	void set_item(const LuaValue& key, const LuaValueVector& value);

    // General high-performance helpers (Phase 2)
    void table_insert(const LuaValue& value);
    void table_insert(long long pos, const LuaValue& value);

	// Metamethod and property cache
	LuaValue* cached_index = nullptr;
	LuaValue* cached_newindex = nullptr;
	bool metamethods_initialized = false;
	void ensure_metamethods() {
		if (metamethods_initialized) return;
		if (metatable) {
			cached_index = metatable->find_prop("__index");
			cached_newindex = metatable->find_prop("__newindex");
		} else {
			cached_index = nullptr;
			cached_newindex = nullptr;
		}
		metamethods_initialized = true;
	}

	void invalidate_metamethods() {
		metamethods_initialized = false;
	}

	// Internal property accessors
	LuaValue* find_prop(const LuaValue& key);
	LuaValue* find_prop(std::string_view key);
	LuaValue* find_prop(const char* key) { return find_prop(std::string_view(key)); }
	void set_prop(const LuaValue& key, const LuaValue& value);
	void set_prop(std::string_view key, const LuaValue& value);
	void set_prop(const char* key, const LuaValue& value) { set_prop(std::string_view(key), value); }

	void set_metatable(const std::shared_ptr<LuaObject>& mt);

	static std::string_view intern(std::string_view sv);
    static const LuaValue& get_single_char(unsigned char c);
};

extern std::shared_ptr<LuaObject> _G;

// Global helper functions
void print_value(const LuaValue& value);

inline double get_double(const LuaValue& value) {
	if (const double* val = std::get_if<double>(&value)) [[likely]] {
		return *val;
	}

	if (const long long* val = std::get_if<long long>(&value)) [[likely]] {
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
	if (std::holds_alternative<long long>(value)) [[likely]] {
		return std::get<long long>(value);
	}
	else if (std::holds_alternative<double>(value)) [[likely]] {
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

inline const std::shared_ptr<LuaObject>& get_object(const LuaValue& value) {
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(value)) [[likely]] {
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
	throw std::runtime_error("Type error: expected table or userdata, got unknown.");
}

inline const std::shared_ptr<LuaObject>& get_object(const std::shared_ptr<LuaObject>& obj) {
	return obj;
}

// Forward declarations for inline helpers
std::string get_lua_type_name(const LuaValue& val);
void call_lua_value(const LuaValue& callable, const LuaValue* args, size_t n_args, LuaValueVector& out_result);

inline LuaCallable* get_callable(const LuaValue& value) {
	if (const auto* callable_ptr = std::get_if<std::shared_ptr<LuaCallable>>(&value)) [[likely]] {
		return callable_ptr->get();
	}
	throw std::runtime_error("attempt to call a " + get_lua_type_name(value) + " value");
}

inline LuaValue lua_call0(const LuaValue& callable, LuaValueVector& out) {
	if (const auto* callable_ptr = std::get_if<std::shared_ptr<LuaCallable>>(&callable)) [[likely]] {
		out.clear();
		(*callable_ptr)->call(nullptr, 0, out);
		return out.empty() ? LuaValue(std::monostate{}) : std::move(out[0]);
	}
	call_lua_value(callable, nullptr, 0, out);
	return out.empty() ? LuaValue(std::monostate{}) : std::move(out[0]);
}

inline LuaValue lua_call1(const LuaValue& callable, LuaValueVector& out, const LuaValue& a1) {
	if (const auto* callable_ptr = std::get_if<std::shared_ptr<LuaCallable>>(&callable)) [[likely]] {
		out.clear();
		const LuaValue args[] = {a1};
		(*callable_ptr)->call(args, 1, out);
		return out.empty() ? LuaValue(std::monostate{}) : std::move(out[0]);
	}
	const LuaValue args[] = {a1};
	call_lua_value(callable, args, 1, out);
	return out.empty() ? LuaValue(std::monostate{}) : std::move(out[0]);
}

inline LuaValue lua_call2(const LuaValue& callable, LuaValueVector& out, const LuaValue& a1, const LuaValue& a2) {
	if (const auto* callable_ptr = std::get_if<std::shared_ptr<LuaCallable>>(&callable)) [[likely]] {
		out.clear();
		const LuaValue args[] = {a1, a2};
		(*callable_ptr)->call(args, 2, out);
		return out.empty() ? LuaValue(std::monostate{}) : std::move(out[0]);
	}
	const LuaValue args[] = {a1, a2};
	call_lua_value(callable, args, 2, out);
	return out.empty() ? LuaValue(std::monostate{}) : std::move(out[0]);
}

inline LuaValue lua_call3(const LuaValue& callable, LuaValueVector& out, const LuaValue& a1, const LuaValue& a2, const LuaValue& a3) {
	if (const auto* callable_ptr = std::get_if<std::shared_ptr<LuaCallable>>(&callable)) [[likely]] {
		out.clear();
		const LuaValue args[] = {a1, a2, a3};
		(*callable_ptr)->call(args, 3, out);
		return out.empty() ? LuaValue(std::monostate{}) : std::move(out[0]);
	}
	const LuaValue args[] = {a1, a2, a3};
	call_lua_value(callable, args, 3, out);
	return out.empty() ? LuaValue(std::monostate{}) : std::move(out[0]);
}

// Helpers
std::string to_cpp_string(const LuaValue& value);
std::string to_cpp_string(const LuaValueVector& value);
void append_to_string(const LuaValue& value, std::string& out);
std::string get_lua_type_name(const LuaValue& val);

// Declarations for global Lua functions (Updated Signatures)
void lua_assert(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_collectgarbage(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_dofile(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_ipairs(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_load(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_loadfile(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_next(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_pairs(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_rawequal(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_rawlen(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_rawget(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_rawset(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_select(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_warn(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_xpcall(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_print(const LuaValue* args, size_t n_args, LuaValueVector& out_result); // Added for completeness

// Iterators
void pairs_iterator(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void ipairs_iterator(const LuaValue* args, size_t n_args, LuaValueVector& out_result);
void lua_tonumber(const LuaValue* args, size_t n_args, LuaValueVector& out);

// Core Call Function
void call_lua_value(const LuaValue& callable, const LuaValue* args, size_t n_args, LuaValueVector& out_result);

// Overloads for convenience
inline void call_lua_value(const LuaValue& callable, LuaValueVector& out_result,
                           const LuaValueVector& args) {
	call_lua_value(callable, args.data(), args.size(), out_result);
}

// Template for variadic arguments (creating vector on stack)
template <typename... Args>
inline void call_lua_value(const LuaValue& callable, LuaValueVector& out_result, Args&&... args) {
	if constexpr (sizeof...(args) == 0) {
		call_lua_value(callable, nullptr, 0, out_result);
	}
	else {
		const LuaValue stack_args[] = {LuaValue(std::forward<Args>(args))...};
		call_lua_value(callable, stack_args, sizeof...(args), out_result);
	}
}

LuaValue lua_get_member(const LuaValue& base, const LuaValue& key);
LuaValue lua_get_member(const LuaValue& base, std::string_view key);
LuaValue lua_get_member(const std::shared_ptr<LuaObject>& base, std::string_view key);
LuaValue lua_get_member(LuaObject* base, std::string_view key);
LuaValue lua_get_member(const LuaValue& base, long long key);
LuaValue lua_get_member(const std::shared_ptr<LuaObject>& base, long long key);

inline LuaValue lua_get_member(const LuaValue& base, const std::string& key) { return lua_get_member(base, std::string_view(key)); }
inline LuaValue lua_get_member(const LuaValue& base, const char* key) { return lua_get_member(base, std::string_view(key)); }

inline LuaValue lua_get_member(const std::shared_ptr<LuaObject>& base, const std::string& key) { return lua_get_member(base, std::string_view(key)); }
inline LuaValue lua_get_member(const std::shared_ptr<LuaObject>& base, const char* key) { return lua_get_member(base, std::string_view(key)); }

inline LuaValue lua_get_member(LuaObject* base, const std::string& key) { return lua_get_member(base, std::string_view(key)); }
inline LuaValue lua_get_member(LuaObject* base, const char* key) { return lua_get_member(base, std::string_view(key)); }
LuaValue lua_get_length(const LuaValue& val); 

// General High-Performance Helpers (Phase 2)
// These are designed for broad usage, not just transpiler tailoring.
void lua_table_insert(const LuaValue& t, const LuaValue& v);
void lua_table_insert(const LuaValue& t, long long pos, const LuaValue& v);
void lua_string_byte(const LuaValue& str, long long i, long long j, LuaValueVector& out);
LuaValue lua_string_sub(const LuaValue& str, long long i, long long j);

inline bool is_lua_truthy(bool val) { return val; }
inline bool is_lua_truthy(long long val) { return val != -1; } // -1 is sentinel for nil in raw byte ops
inline bool is_lua_truthy(double val) { return true; } // Numbers are always truthy in Lua (except sentinel)
inline bool is_lua_truthy(const LuaValue& val) {
	auto idx = val.index();
	if (idx == INDEX_NIL) [[unlikely]] return false;
	if (idx == INDEX_BOOLEAN) return std::get<bool>(val);
	return true;
}

// ==========================================
// Inline Character Predicates for Tokenizer Optimization
// These avoid call_lua_value overhead for character checks
// ==========================================

inline char lua_get_char(const LuaValue& v) {
	if (const auto* i = std::get_if<long long>(&v)) [[likely]] {
		return static_cast<char>(*i);
	}
	if (const auto* sv = std::get_if<std::string_view>(&v)) {
		return sv->size() == 1 ? (*sv)[0] : '\0';
	}
	if (const auto* s = std::get_if<std::string>(&v)) {
		return s->size() == 1 ? (*s)[0] : '\0';
	}
	if (const auto* d = std::get_if<double>(&v)) {
		return static_cast<char>(*d);
	}
	return '\0';
}

// Character predicates (primitive overloads for performance)
inline bool lua_is_digit(unsigned char c) { return c >= '0' && c <= '9'; }
inline bool lua_is_alpha(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
inline bool lua_is_whitespace(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
inline bool lua_is_hex_digit(unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
inline bool lua_is_alnum(unsigned char c) { return lua_is_digit(c) || lua_is_alpha(c); }

inline bool lua_is_digit(long long c) { return c != -1 && lua_is_digit(static_cast<unsigned char>(c)); }
inline bool lua_is_alpha(long long c) { return c != -1 && lua_is_alpha(static_cast<unsigned char>(c)); }
inline bool lua_is_whitespace(long long c) { return c != -1 && lua_is_whitespace(static_cast<unsigned char>(c)); }
inline bool lua_is_hex_digit(long long c) { return c != -1 && lua_is_hex_digit(static_cast<unsigned char>(c)); }
inline bool lua_is_alnum(long long c) { return c != -1 && lua_is_alnum(static_cast<unsigned char>(c)); }

inline bool lua_is_digit(const LuaValue& v) {
	char c = lua_get_char(v);
	return lua_is_digit(static_cast<unsigned char>(c));
}

inline bool lua_is_alpha(const LuaValue& v) {
	char c = lua_get_char(v);
	return lua_is_alpha(static_cast<unsigned char>(c));
}

inline bool lua_is_whitespace(const LuaValue& v) {
	char c = lua_get_char(v);
	return lua_is_whitespace(static_cast<unsigned char>(c));
}

inline bool lua_is_hex_digit(const LuaValue& v) {
	char c = lua_get_char(v);
	return lua_is_hex_digit(static_cast<unsigned char>(c));
}

inline bool lua_is_alnum(const LuaValue& v) {
	char c = lua_get_char(v);
	return lua_is_alnum(static_cast<unsigned char>(c));
}

bool operator<=(const LuaValue& lhs, const LuaValue& rhs);

// Arithmetic Overloads
LuaValue operator-(const LuaValue& a);
LuaValue operator+(const LuaValue& a, const LuaValue& b);
LuaValue operator-(const LuaValue& a, const LuaValue& b);
LuaValue operator*(const LuaValue& a, const LuaValue& b);
LuaValue operator/(const LuaValue& a, const LuaValue& b);
LuaValue operator%(const LuaValue& a, const LuaValue& b);

// Bitwise Overloads
LuaValue operator~(const LuaValue& a);
LuaValue operator&(const LuaValue& a, const LuaValue& b);
LuaValue operator|(const LuaValue& a, const LuaValue& b);
LuaValue operator^(const LuaValue& a, const LuaValue& b);
LuaValue operator<<(const LuaValue& a, const LuaValue& b);
LuaValue operator>>(const LuaValue& a, const LuaValue& b);

// Comparison Helpers
bool lua_equals(const LuaValue& a, const LuaValue& b);
bool lua_not_equals(const LuaValue& a, const LuaValue& b);
bool lua_less_than(const LuaValue& a, const LuaValue& b);
bool lua_greater_than(const LuaValue& a, const LuaValue& b);
bool lua_less_equals(const LuaValue& a, const LuaValue& b);
bool lua_greater_equals(const LuaValue& a, const LuaValue& b);


// Optimized Single-Character Access (replaces str:sub(i, i))
inline const LuaValue& lua_string_char_at(const LuaValue& str, long long i) {
	if (const auto* s = std::get_if<std::string>(&str)) {
		if (i >= 1 && i <= static_cast<long long>(s->size())) {
			return LuaObject::get_single_char(static_cast<unsigned char>((*s)[i - 1]));
		}
	} else if (const auto* sv = std::get_if<std::string_view>(&str)) {
		if (i >= 1 && i <= static_cast<long long>(sv->size())) {
			return LuaObject::get_single_char(static_cast<unsigned char>((*sv)[i - 1]));
		}
	}
    static const LuaValue empty_str{std::string("")};
	return empty_str;
}

inline const LuaValue& lua_string_char_at(const LuaValue& str, const LuaValue& pos) {
	long long i = 0;
	if (const auto* l = std::get_if<long long>(&pos)) i = *l;
	else if (const auto* d = std::get_if<double>(&pos)) i = static_cast<long long>(*d);
	else { static const LuaValue empty_str{std::string("")}; return empty_str; }
    return lua_string_char_at(str, i);
}

// Optimized Byte Access (replaces str:byte(i))
inline long long lua_string_byte_at_raw(const LuaValue& str, long long i) {
	if (const auto* s = std::get_if<std::string>(&str)) {
		if (i >= 1 && i <= static_cast<long long>(s->size())) {
			return static_cast<long long>(static_cast<unsigned char>((*s)[i - 1]));
		}
	} else if (const auto* sv = std::get_if<std::string_view>(&str)) {
		if (i >= 1 && i <= static_cast<long long>(sv->size())) {
			return static_cast<long long>(static_cast<unsigned char>((*sv)[i - 1]));
		}
	}
	return -1; // Indicate nil/out of bounds
}

inline long long lua_string_byte_at_raw(const LuaValue& str, const LuaValue& pos) {
    long long i = 0;
    if (const auto* l = std::get_if<long long>(&pos)) i = *l;
    else if (const auto* d = std::get_if<double>(&pos)) i = static_cast<long long>(*d);
    else return -1;
    return lua_string_byte_at_raw(str, i);
}

inline LuaValue lua_string_byte_at(const LuaValue& str, long long i) {
    long long b = lua_string_byte_at_raw(str, i);
    if (b == -1) return std::monostate{};
    return b;
}

inline LuaValue lua_string_byte_at(const LuaValue& str, const LuaValue& pos) {
	long long i = 0;
	if (const auto* l = std::get_if<long long>(&pos)) i = *l;
	else if (const auto* d = std::get_if<double>(&pos)) i = static_cast<long long>(*d);
	else return std::monostate{}; 
    return lua_string_byte_at(str, i);
}

// Optimized String View Overloads
inline bool lua_equals(const LuaValue& a, std::string_view b) {
	if (const auto* s = std::get_if<std::string>(&a)) return *s == b;
	if (const auto* sv = std::get_if<std::string_view>(&a)) return *sv == b;
	return false;
}
inline bool lua_equals(std::string_view a, const LuaValue& b) { return lua_equals(b, a); }
inline bool lua_equals(const LuaValue& a, const std::string& b) { return lua_equals(a, std::string_view(b)); }
inline bool lua_equals(const std::string& a, const LuaValue& b) { return lua_equals(b, std::string_view(a)); }
inline bool lua_equals(const LuaValue& a, const char* b) { return b ? lua_equals(a, std::string_view(b)) : std::holds_alternative<std::monostate>(a); }
inline bool lua_equals(const char* a, const LuaValue& b) { return a ? lua_equals(b, std::string_view(a)) : std::holds_alternative<std::monostate>(b); }

// Numeric Overloads to avoid ambiguity with literal 0 and improve performance
inline bool lua_equals(const LuaValue& a, double b) {
	if (const auto* d = std::get_if<double>(&a)) return *d == b;
	if (const auto* l = std::get_if<long long>(&a)) return static_cast<double>(*l) == b;
	return false;
}
inline bool lua_equals(double a, const LuaValue& b) { return lua_equals(b, a); }
inline bool lua_equals(const LuaValue& a, long long b) {
	if (const auto* l = std::get_if<long long>(&a)) return *l == b;
	if (const auto* d = std::get_if<double>(&a)) return *d == static_cast<double>(b);
	return false;
}
inline bool lua_equals(long long a, const LuaValue& b) { return lua_equals(b, a); }
inline bool lua_equals(const LuaValue& a, int b) { return lua_equals(a, static_cast<long long>(b)); }
inline bool lua_equals(int a, const LuaValue& b) { return lua_equals(b, static_cast<long long>(a)); }

inline bool lua_not_equals(const LuaValue& a, std::string_view b) { return !lua_equals(a, b); }
inline bool lua_not_equals(std::string_view a, const LuaValue& b) { return !lua_equals(b, a); }
inline bool lua_not_equals(const LuaValue& a, const std::string& b) { return !lua_equals(a, std::string_view(b)); }
inline bool lua_not_equals(const std::string& a, const LuaValue& b) { return !lua_equals(b, std::string_view(a)); }
inline bool lua_not_equals(const LuaValue& a, const char* b) { return !lua_equals(a, b); }
inline bool lua_not_equals(const char* a, const LuaValue& b) { return !lua_equals(b, a); }
inline bool lua_not_equals(const LuaValue& a, double b) { return !lua_equals(a, b); }
inline bool lua_not_equals(double a, const LuaValue& b) { return !lua_equals(b, a); }
inline bool lua_not_equals(const LuaValue& a, long long b) { return !lua_equals(a, b); }
inline bool lua_not_equals(long long a, const LuaValue& b) { return !lua_equals(b, a); }
inline bool lua_not_equals(const LuaValue& a, int b) { return !lua_equals(a, b); }
inline bool lua_not_equals(int a, const LuaValue& b) { return !lua_equals(b, a); }

inline bool lua_less_than(const LuaValue& a, std::string_view b) {
	if (const auto* s = std::get_if<std::string>(&a)) return *s < b;
	if (const auto* sv = std::get_if<std::string_view>(&a)) return *sv < b;
	return lua_less_than(a, LuaValue(b));
}
inline bool lua_less_than(std::string_view a, const LuaValue& b) {
	if (const auto* s = std::get_if<std::string>(&b)) return a < *s;
	if (const auto* sv = std::get_if<std::string_view>(&b)) return a < *sv;
	return lua_less_than(LuaValue(a), b);
}
inline bool lua_less_than(const LuaValue& a, const std::string& b) { return lua_less_than(a, std::string_view(b)); }
inline bool lua_less_than(const std::string& a, const LuaValue& b) { return lua_less_than(std::string_view(a), b); }
inline bool lua_less_than(const LuaValue& a, const char* b) { return b ? lua_less_than(a, std::string_view(b)) : false; }
inline bool lua_less_than(const char* a, const LuaValue& b) { return a ? lua_less_than(std::string_view(a), b) : false; }

inline bool lua_less_than(const LuaValue& a, double b) {
	if (const auto* d = std::get_if<double>(&a)) return *d < b;
	if (const auto* l = std::get_if<long long>(&a)) return static_cast<double>(*l) < b;
	return false;
}
inline bool lua_less_than(double a, const LuaValue& b) {
	if (const auto* d = std::get_if<double>(&b)) return a < *d;
	if (const auto* l = std::get_if<long long>(&b)) return a < static_cast<double>(*l);
	return false;
}
inline bool lua_less_than(const LuaValue& a, long long b) {
	if (const auto* l = std::get_if<long long>(&a)) return *l < b;
	if (const auto* d = std::get_if<double>(&a)) return *d < static_cast<double>(b);
	return false;
}
inline bool lua_less_than(long long a, const LuaValue& b) {
	if (const auto* l = std::get_if<long long>(&b)) return a < *l;
	if (const auto* d = std::get_if<double>(&b)) return static_cast<double>(a) < *d;
	return false;
}
inline bool lua_less_than(const LuaValue& a, int b) { return lua_less_than(a, static_cast<long long>(b)); }
inline bool lua_less_than(int a, const LuaValue& b) { return lua_less_than(static_cast<long long>(a), b); }

inline bool lua_greater_than(const LuaValue& a, std::string_view b) { return lua_less_than(b, a); }
inline bool lua_greater_than(std::string_view a, const LuaValue& b) { return lua_less_than(b, a); }
inline bool lua_greater_than(const LuaValue& a, const std::string& b) { return lua_less_than(std::string_view(b), a); }
inline bool lua_greater_than(const std::string& a, const LuaValue& b) { return lua_less_than(b, std::string_view(a)); }
inline bool lua_greater_than(const LuaValue& a, const char* b) { return b ? lua_less_than(std::string_view(b), a) : false; }
inline bool lua_greater_than(const char* a, const LuaValue& b) { return a ? lua_less_than(b, std::string_view(a)) : false; }
inline bool lua_greater_than(const LuaValue& a, double b) { return lua_less_than(b, a); }
inline bool lua_greater_than(double a, const LuaValue& b) { return lua_less_than(b, a); }
inline bool lua_greater_than(const LuaValue& a, long long b) { return lua_less_than(b, a); }
inline bool lua_greater_than(long long a, const LuaValue& b) { return lua_less_than(b, a); }
inline bool lua_greater_than(const LuaValue& a, int b) { return lua_less_than(static_cast<long long>(b), a); }
inline bool lua_greater_than(int a, const LuaValue& b) { return lua_less_than(b, static_cast<long long>(a)); }

bool lua_less_equals(const LuaValue& a, const LuaValue& b);
inline bool lua_less_equals(const LuaValue& a, std::string_view b) {
	if (const auto* s = std::get_if<std::string>(&a)) return *s <= b;
	if (const auto* sv = std::get_if<std::string_view>(&a)) return *sv <= b;
	return lua_less_equals(a, LuaValue(b));
}
inline bool lua_less_equals(std::string_view a, const LuaValue& b) {
	if (const auto* s = std::get_if<std::string>(&b)) return a <= *s;
	if (const auto* sv = std::get_if<std::string_view>(&b)) return a <= *sv;
	return lua_less_equals(LuaValue(a), b);
}
inline bool lua_less_equals(const LuaValue& a, const char* b) { return b ? lua_less_equals(a, std::string_view(b)) : false; }
inline bool lua_less_equals(const char* a, const LuaValue& b) { return a ? lua_less_equals(std::string_view(a), b) : false; }
inline bool lua_less_equals(const LuaValue& a, double b) {
	if (const auto* d = std::get_if<double>(&a)) return *d <= b;
	if (const auto* l = std::get_if<long long>(&a)) return static_cast<double>(*l) <= b;
	return lua_less_equals(a, LuaValue(b));
}
inline bool lua_less_equals(double a, const LuaValue& b) {
	if (const auto* d = std::get_if<double>(&b)) return a <= *d;
	if (const auto* l = std::get_if<long long>(&b)) return a <= static_cast<double>(*l);
	return lua_less_equals(LuaValue(a), b);
}
inline bool lua_less_equals(const LuaValue& a, long long b) {
	if (const auto* l = std::get_if<long long>(&a)) return *l <= b;
	if (const auto* d = std::get_if<double>(&a)) return *d <= static_cast<double>(b);
	return lua_less_equals(a, LuaValue(b));
}
inline bool lua_less_equals(long long a, const LuaValue& b) {
	if (const auto* l = std::get_if<long long>(&b)) return a <= *l;
	if (const auto* d = std::get_if<double>(&b)) return static_cast<double>(a) <= *d;
	return lua_less_equals(LuaValue(a), b);
}
inline bool lua_less_equals(const LuaValue& a, int b) { return lua_less_equals(a, static_cast<long long>(b)); }
inline bool lua_less_equals(int a, const LuaValue& b) { return lua_less_equals(static_cast<long long>(a), b); }

bool lua_greater_equals(const LuaValue& a, const LuaValue& b);
inline bool lua_greater_equals(const LuaValue& a, std::string_view b) { return !lua_less_than(a, b); }
inline bool lua_greater_equals(std::string_view a, const LuaValue& b) { return !lua_less_than(a, b); }
inline bool lua_greater_equals(const LuaValue& a, const char* b) { return b ? !lua_less_than(a, b) : false; }
inline bool lua_greater_equals(const char* a, const LuaValue& b) { return a ? !lua_less_than(b, a) : false; }
inline bool lua_greater_equals(const LuaValue& a, double b) { return !lua_less_than(a, b); }
inline bool lua_greater_equals(double a, const LuaValue& b) { return !lua_less_than(a, b); }
inline bool lua_greater_equals(const LuaValue& a, long long b) { return !lua_less_than(a, b); }
inline bool lua_greater_equals(long long a, const LuaValue& b) { return !lua_less_than(a, b); }
inline bool lua_greater_equals(const LuaValue& a, int b) { return !lua_less_than(a, static_cast<long long>(b)); }
inline bool lua_greater_equals(int a, const LuaValue& b) { return !lua_less_than(static_cast<long long>(a), b); }

LuaValue lua_concat(const LuaValue& a, const LuaValue& b);
LuaValue lua_concat(LuaValue&& a, const LuaValue& b);
LuaValue as_view(const LuaValue& v);

extern std::shared_ptr<LuaObject> _G;

inline double to_double(const LuaValue& v) {
	if (const double* d = std::get_if<double>(&v)) [[likely]] return *d;
	if (const long long* i = std::get_if<long long>(&v)) [[likely]] return static_cast<double>(*i);
	if (const std::string* s = std::get_if<std::string>(&v)) {
		char* end;
		double res = std::strtod(s->c_str(), &end);
		if (end != s->c_str()) return res;
	}
	if (const std::string_view* sv = std::get_if<std::string_view>(&v)) {
		std::string tmp(*sv);
		char* end;
		double res = std::strtod(tmp.c_str(), &end);
		if (end != tmp.c_str()) return res;
	}
	return 0.0;
}

inline bool is_integer(const LuaValue& v) {
	return std::holds_alternative<long long>(v);
}

inline LuaValue operator-(const LuaValue& a) {
	if (const long long* i = std::get_if<long long>(&a)) [[likely]] return -(*i);
	if (const double* d = std::get_if<double>(&a)) [[likely]] return -(*d);
	return -to_double(a);
}

inline LuaValue operator+(const LuaValue& a, const LuaValue& b) {
	if (const long long* ai = std::get_if<long long>(&a)) [[likely]] {
		if (const long long* bi = std::get_if<long long>(&b)) [[likely]] return *ai + *bi;
	}
	return to_double(a) + to_double(b);
}

inline LuaValue operator+(const LuaValue& a, double b) { return to_double(a) + b; }
inline LuaValue operator+(double a, const LuaValue& b) { return a + to_double(b); }
inline LuaValue operator+(const LuaValue& a, long long b) {
	if (const long long* ai = std::get_if<long long>(&a)) [[likely]] return *ai + b;
	return to_double(a) + static_cast<double>(b);
}
inline LuaValue operator+(long long a, const LuaValue& b) {
	if (const long long* bi = std::get_if<long long>(&b)) [[likely]] return a + *bi;
	return static_cast<double>(a) + to_double(b);
}
inline LuaValue operator+(const LuaValue& a, int b) { return a + static_cast<long long>(b); }
inline LuaValue operator+(int a, const LuaValue& b) { return static_cast<long long>(a) + b; }

inline LuaValue operator-(const LuaValue& a, const LuaValue& b) {
	if (const long long* ai = std::get_if<long long>(&a)) [[likely]] {
		if (const long long* bi = std::get_if<long long>(&b)) [[likely]] return *ai - *bi;
	}
	return to_double(a) - to_double(b);
}

inline LuaValue operator-(const LuaValue& a, double b) { return to_double(a) - b; }
inline LuaValue operator-(double a, const LuaValue& b) { return a - to_double(b); }
inline LuaValue operator-(const LuaValue& a, long long b) {
	if (const long long* ai = std::get_if<long long>(&a)) [[likely]] return *ai - b;
	return to_double(a) - static_cast<double>(b);
}
inline LuaValue operator-(long long a, const LuaValue& b) {
	if (const long long* bi = std::get_if<long long>(&b)) [[likely]] return a - *bi;
	return static_cast<double>(a) - to_double(b);
}

inline LuaValue operator*(const LuaValue& a, const LuaValue& b) {
	if (const long long* ai = std::get_if<long long>(&a)) [[likely]] {
		if (const long long* bi = std::get_if<long long>(&b)) [[likely]] return *ai * *bi;
	}
	return to_double(a) * to_double(b);
}

inline LuaValue operator*(const LuaValue& a, double b) { return to_double(a) * b; }
inline LuaValue operator*(double a, const LuaValue& b) { return a * to_double(b); }
inline LuaValue operator*(const LuaValue& a, long long b) {
	if (const long long* ai = std::get_if<long long>(&a)) [[likely]] return *ai * b;
	return to_double(a) * static_cast<double>(b);
}
inline LuaValue operator*(long long a, const LuaValue& b) {
	if (const long long* bi = std::get_if<long long>(&b)) [[likely]] return a * *bi;
	return static_cast<double>(a) * to_double(b);
}

inline LuaValue operator/(const LuaValue& a, const LuaValue& b) {
	return to_double(a) / to_double(b);
}
inline LuaValue operator/(const LuaValue& a, double b) { return to_double(a) / b; }
inline LuaValue operator/(double a, const LuaValue& b) { return a / to_double(b); }

inline LuaValue operator%(const LuaValue& a, const LuaValue& b) {
	if (is_integer(a) && is_integer(b)) {
		long long av = std::get<long long>(a);
		long long bv = std::get<long long>(b);
		if (bv == 0) return 0.0;
		return av % bv;
	}
	return std::fmod(to_double(a), to_double(b));
}

inline LuaValue operator~(const LuaValue& a) {
	if (const long long* i = std::get_if<long long>(&a)) return ~(*i);
	return ~(static_cast<long long>(to_double(a)));
}

inline LuaValue operator&(const LuaValue& a, const LuaValue& b) {
	return static_cast<long long>(to_double(a)) & static_cast<long long>(to_double(b));
}

inline LuaValue operator|(const LuaValue& a, const LuaValue& b) {
	return static_cast<long long>(to_double(a)) | static_cast<long long>(to_double(b));
}

inline LuaValue operator^(const LuaValue& a, const LuaValue& b) {
	return static_cast<long long>(to_double(a)) ^ static_cast<long long>(to_double(b));
}

inline LuaValue operator<<(const LuaValue& a, const LuaValue& b) {
	return static_cast<long long>(to_double(a)) << static_cast<long long>(to_double(b));
}

inline LuaValue operator>>(const LuaValue& a, const LuaValue& b) {
	return static_cast<long long>(to_double(a)) >> static_cast<long long>(to_double(b));
}

inline LuaValue lua_get_length(const LuaValue& val) {
	if (auto* s = std::get_if<std::string>(&val)) return static_cast<double>(s->length());
	if (auto* sv = std::get_if<std::string_view>(&val)) return static_cast<double>(sv->length());
	if (auto* obj_ptr = std::get_if<std::shared_ptr<LuaObject>>(&val)) {
		auto& obj = *obj_ptr;
		if (obj->metatable) {
			auto len_meta = obj->metatable->get_item("__len");
			if (!std::holds_alternative<std::monostate>(len_meta)) {
				LuaValueVector res;
				call_lua_value(len_meta, &val, 1, res);
				return res.empty() ? std::monostate{} : res[0];
			}
		}
		return static_cast<double>(obj->array_part.size());
	}
	throw std::runtime_error("attempt to get length of a " + get_lua_type_name(val) + " value");
}

inline LuaValue lua_get_member(const LuaValue& base, const LuaValue& key) {
	switch (base.index()) {
	case INDEX_OBJECT: {
		return std::get<INDEX_OBJECT>(base)->get_item(key);
	}
	case INDEX_STRING:
	case INDEX_STRING_VIEW: {
		static LuaObject* string_lib_obj = std::get<std::shared_ptr<LuaObject>>(_G->get_item("string")).get();
		return string_lib_obj->get_item(key);
	}
	default:
		throw std::runtime_error("attempt to index a " + get_lua_type_name(base) + " value");
	}
}

inline LuaValue lua_get_member(const LuaValue& base, std::string_view key) {
	switch (base.index()) {
	case INDEX_OBJECT: {
		return std::get<INDEX_OBJECT>(base)->get_item(key);
	}
	case INDEX_STRING:
	case INDEX_STRING_VIEW: {
		static LuaObject* string_lib_obj = std::get<std::shared_ptr<LuaObject>>(_G->get_item("string")).get();
		return string_lib_obj->get_item(key);
	}
	default:
		throw std::runtime_error("attempt to index a " + get_lua_type_name(base) + " value");
	}
}

inline LuaValue lua_get_member(const std::shared_ptr<LuaObject>& base, std::string_view key) {
	if (!base) return std::monostate{};
	return base->get_item(key);
}

inline LuaValue lua_get_member(LuaObject* base, std::string_view key) {
	if (!base) return std::monostate{};
	return base->get_item(key);
}

inline LuaValue lua_get_member(const LuaValue& base, long long key) {
	if (const auto* obj = std::get_if<std::shared_ptr<LuaObject>>(&base)) {
		return (*obj)->get_item(key);
	}
	return std::monostate{};
}

inline LuaValue lua_get_member(const std::shared_ptr<LuaObject>& base, long long key) {
	if (!base) return std::monostate{};
	return base->get_item(key);
}


// Transpiler helper to safely extract return values
inline LuaValue get_return_value(LuaValueVector& results, size_t index) {
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
