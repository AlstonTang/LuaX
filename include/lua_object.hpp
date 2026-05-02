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
#include <atomic>

// Forward declaration
class LuaObject;

class LuaRefCounted {
    mutable std::atomic<int> ref_count{0};
public:
    virtual ~LuaRefCounted() = default;
    void retain() const { ref_count.fetch_add(1, std::memory_order_relaxed); }
    void release() const {
        if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }
};

struct LuaString : public LuaRefCounted {
    std::string str;
    LuaString(std::string_view s) : str(s) {}
};

// Virtual Base Class for all callable entities (Functions, Closures, C++ built-ins)
struct LuaCallable : public LuaRefCounted {
	virtual ~LuaCallable() = default;
	
	// Core signature for variadic/multi-return calls
	virtual void call(const LuaValue* args, size_t n_args, LuaValueVector& out_result) = 0;

	// Optimization: Direct overloads for single-return calls
	virtual LuaValue call0(LuaValueVector& out);
	virtual LuaValue call1(LuaValueVector& out, const LuaValue& a1);
	virtual LuaValue call2(LuaValueVector& out, const LuaValue& a1, const LuaValue& a2);
	virtual LuaValue call3(LuaValueVector& out, const LuaValue& a1, const LuaValue& a2, const LuaValue& a3);
};

// Generic template to wrap ANY lambda/callable without std::function overhead
template <typename F>
struct LuaLambdaCallable final : public LuaCallable {
	F func;
	LuaLambdaCallable(F&& f) : func(std::forward<F>(f)) {}

	void call(const LuaValue* args, size_t n_args, LuaValueVector& out_result) override {
		func(args, n_args, out_result);
	}
};

template <typename F>
inline LuaCallable* make_lua_callable(F&& f) {
    return new LuaLambdaCallable<F>(std::forward<F>(f));
}

// Specialized templates for functions with known arity to provide direct overrides
template <size_t Arity, typename FVar, typename... FExt>
struct LuaSpecializedCallable;

template <typename FVar, typename F0>
struct LuaSpecializedCallable<0, FVar, F0> final : public LuaCallable {
	FVar f_var; F0 f0;
	LuaSpecializedCallable(FVar&& v, F0&& s) : f_var(std::forward<FVar>(v)), f0(std::forward<F0>(s)) {}
	void call(const LuaValue* args, size_t n, LuaValueVector& out) override { f_var(args, n, out); }
	LuaValue call0(LuaValueVector& /*out*/) override { return f0(); }
};

template <typename FVar, typename F1>
struct LuaSpecializedCallable<1, FVar, F1> final : public LuaCallable {
	FVar f_var; F1 f1;
	LuaSpecializedCallable(FVar&& v, F1&& s) : f_var(std::forward<FVar>(v)), f1(std::forward<F1>(s)) {}
	void call(const LuaValue* args, size_t n, LuaValueVector& out) override { f_var(args, n, out); }
	LuaValue call1(LuaValueVector& /*out*/, const LuaValue& a1) override { return f1(a1); }
};

template <typename FVar, typename F2>
struct LuaSpecializedCallable<2, FVar, F2> final : public LuaCallable {
	FVar f_var; F2 f2;
	LuaSpecializedCallable(FVar&& v, F2&& s) : f_var(std::forward<FVar>(v)), f2(std::forward<F2>(s)) {}
	void call(const LuaValue* args, size_t n, LuaValueVector& out) override { f_var(args, n, out); }
	LuaValue call2(LuaValueVector& /*out*/, const LuaValue& a1, const LuaValue& a2) override { return f2(a1, a2); }
};

template <typename FVar, typename F3>
struct LuaSpecializedCallable<3, FVar, F3> final : public LuaCallable {
	FVar f_var; F3 f3;
	LuaSpecializedCallable(FVar&& v, F3&& s) : f_var(std::forward<FVar>(v)), f3(std::forward<F3>(s)) {}
	void call(const LuaValue* args, size_t n, LuaValueVector& out) override { f_var(args, n, out); }
	LuaValue call3(LuaValueVector& /*out*/, const LuaValue& a1, const LuaValue& a2, const LuaValue& a3) override { return f3(a1, a2, a3); }
};

template <size_t Arity, typename FVar, typename FSpec>
inline LuaCallable* make_specialized_callable(FVar&& v, FSpec&& s) {
    return new LuaSpecializedCallable<Arity, FVar, FSpec>(std::forward<FVar>(v), std::forward<FSpec>(s));
}

// LuaObject Definition
class LuaObject : public LuaRefCounted {
public:
	struct MetamethodRef {
		const LuaValue* index;
		const LuaValue* newindex;
	};
	
	virtual ~LuaObject() = default;
	
	// Hybrid storage: small vector for few properties, map for many.
	struct PropPair {
		LuaValue first;
		LuaValue second;
	};
	std::vector<PropPair, PoolAllocator<PropPair>> small_props;
	using PropMap = std::unordered_map<LuaValue, LuaValue, LuaValueHash, LuaValueEq, PoolAllocator<std::pair<const LuaValue, LuaValue>>>;
	std::unique_ptr<PropMap> properties;
	
	std::vector<LuaValue, PoolAllocator<LuaValue>> array_part;
	LuaObject* metatable = nullptr;
	
	static const size_t SMALL_TABLE_THRESHOLD = 8; // Shrink threshold to keep small_props small

	static LuaValue intern_key(const LuaValue& v) {
		if (v.index() == INDEX_STRING) return intern(v.get<std::string_view>());
		if (v.index() == INDEX_STRING_VIEW) return intern(v.get<std::string_view>());
		return v;
	}

	static LuaObject* create(
		std::initializer_list<PropPair> props = {},
		std::initializer_list<LuaValue> arr = {},
		LuaObject* mt = nullptr
	);

	LuaValue get(std::string_view key);
	LuaValue get(const std::string& key) { return get(std::string_view(key)); }
	LuaValue get(const char* key) { return get(std::string_view(key)); }
	// Overload for LuaValue key (extracts string from variant)
	inline LuaValue get(const LuaValue& key) {
		auto idx = key.index();
		if (idx == INDEX_STRING) {
			return get(std::string_view(key.get<std::string_view>()));
		}
		if (idx == INDEX_STRING_VIEW) {
			return get(key.get<std::string_view>());
		}
		return get_item(key);
	}

	void set(std::string_view key, const LuaValue& value);
	void set(const std::string& key, const LuaValue& value) { set(std::string_view(key), value); }
	void set(const char* key, const LuaValue& value) { set(std::string_view(key), value); }
	void set(const char* key, LuaCFunction value) { set(std::string_view(key), LuaValue(value)); }
	// Overload for LuaValue key (extracts string from variant)
	inline void set(const LuaValue& key, const LuaValue& value) {
		auto idx = key.index();
		if (idx == INDEX_STRING) {
			set(std::string_view(key.get<std::string_view>()), value);
		} else if (idx == INDEX_STRING_VIEW) {
			set(key.get<std::string_view>(), value);
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
	LuaValue cached_index;
	LuaValue cached_newindex;
	std::atomic<bool> metamethods_initialized{false}; 
	
	// Inside class LuaObject:
	inline MetamethodRef get_cached_metamethods() {
		// Acquire-Release ensures thread safety for the initialization check
		if (!metamethods_initialized.load(std::memory_order_acquire)) {
			ensure_metamethods();
		}
		return { &cached_index, &cached_newindex };
	}

	void ensure_metamethods() {
		if (metamethods_initialized) return;
		if (metatable) {
			cached_index = metatable->get_prop("__index");
			cached_newindex = metatable->get_prop("__newindex");
		} else {
			cached_index = LuaValue();
			cached_newindex = LuaValue();
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

	LuaValue get_prop(const LuaValue& key);
	LuaValue get_prop(std::string_view key);
	LuaValue get_prop(const char* key) { return get_prop(std::string_view(key)); }

	void set_prop(const LuaValue& key, const LuaValue& value);
	void set_prop(std::string_view key, const LuaValue& value);
	void set_prop(const char* key, const LuaValue& value) { set_prop(std::string_view(key), value); }

	void set_metatable(LuaObject* mt);

	static std::string_view intern(std::string_view sv);
	static const LuaValue& get_single_char(unsigned char c);
private:
	// Internal version that tracks depth to prevent Segfaults
	LuaValue get_item_internal(const LuaValue& key, int depth);
};

extern LuaObject* _G;

// Global helper functions
void print_value(const LuaValue& value);
void luax_cleanup();

template <typename T, typename U>
inline long long lua_floor_div(T a, U b) {
	if (b == 0) [[unlikely]] return 0;
	double res = std::floor(static_cast<double>(a) / static_cast<double>(b));
	return static_cast<long long>(res);
}

template <typename T, typename U>
inline long long lua_mod(T a, U b) {
	if (b == 0) [[unlikely]] return 0;
	long long res = static_cast<long long>(a) % static_cast<long long>(b);
	if ((res ^ static_cast<long long>(b)) < 0 && res != 0) res += static_cast<long long>(b);
	return res;
}

inline double lua_mod(double a, double b) {
	if (b == 0) [[unlikely]] return 0;
	double res = std::fmod(a, b);
	if (res != 0 && ((res < 0) != (b < 0))) res += b;
	return res;
}

inline double get_double(const LuaValue& value) {
	size_t idx = value.index();
	if (idx == INDEX_DOUBLE || idx == INDEX_INTEGER) return value.get<double>();
	if (idx == INDEX_STRING) {
		std::string_view sv = value.get<std::string_view>();
		if (!sv.empty()) {
			std::string s(sv);
			char* end;
			double result = std::strtod(s.c_str(), &end);
			if (end != s.c_str()) return result;
		}
	}
	throw std::runtime_error("Type error: expected number.");
}

inline std::string_view get_string_view(const LuaValue& value) {
	if (value.index() == INDEX_STRING) return value.get<std::string_view>();
	throw std::runtime_error("Type error: expected string.");
}

inline long long get_long_long(const LuaValue& value) {
	size_t idx = value.index();
	if (idx == INDEX_INTEGER || idx == INDEX_DOUBLE) return value.get<long long>();
	if (idx == INDEX_STRING) {
		try {
			return std::stoll(std::string(value.get<std::string_view>()));
		} catch (...) {}
	}
	throw std::runtime_error("Type error: expected integer.");
}

inline LuaObject* get_object(const LuaValue& value) {
	if (value.index() == INDEX_OBJECT) return value.get<LuaObject*>();
	throw std::runtime_error("Type error: expected table or userdata.");
}

inline LuaObject* get_object(LuaObject* obj) {
	return obj;
}

// Forward declarations for inline helpers
std::string get_lua_type_name(const LuaValue& val);

// Forward declaration — full definition below after is_lua_truthy
inline void call_lua_value(const LuaValue& callable, const LuaValue* args, size_t n_args, LuaValueVector& out_result);

// A lightweight shim to make a C function look like a LuaCallable
struct LuaCFunctionShim : public LuaCallable {
	LuaCFunctionPtr ptr;
	void call(const LuaValue* args, size_t n_args, LuaValueVector& out_result) override {
		((LuaCFunctionTyped)ptr)(args, n_args, out_result);
	}
};

inline LuaCallable* get_callable(const LuaValue& value) {
	const size_t idx = value.index();

	if (idx == INDEX_FUNCTION) [[likely]] return value.get<LuaCallable*>();

	if (idx == INDEX_CFUNCTION) [[likely]] {
		thread_local LuaCFunctionShim shim;
		shim.ptr = value.get<LuaCFunction>().ptr;
		return &shim;
	}

	if (idx == INDEX_OBJECT) [[unlikely]] {
		auto* obj = value.get<LuaObject*>();
		if (obj && obj->metatable) {
			LuaValue call_handler = obj->metatable->get_item("__call");
			if (call_handler.index() == INDEX_FUNCTION || call_handler.index() == INDEX_CFUNCTION) {
				return get_callable(call_handler);
			}
		}
	}

	[[unlikely]]
	throw std::runtime_error("attempt to call a " + get_lua_type_name(value) + " value");
}

inline LuaValue lua_call0(const LuaValue& callable, LuaValueVector& out) {
    if (callable.index() == INDEX_CFUNCTION) {
        auto cfunc = callable.get<LuaCFunction>();
        out.clear();
        ((LuaCFunctionTyped)cfunc.ptr)(nullptr, 0, out);
        return out.empty() ? LuaValue() : std::move(out[0]);
    }
    if (callable.index() == INDEX_FUNCTION) {
        return callable.get<LuaCallable*>()->call0(out);
    }
    call_lua_value(callable, nullptr, 0, out);
    return out.empty() ? LuaValue() : std::move(out[0]);
}

inline LuaValue lua_call1(const LuaValue& callable, LuaValueVector& out, const LuaValue& a1) {
	if (callable.index() == INDEX_CFUNCTION) {
		auto cfunc = callable.get<LuaCFunction>();
		const LuaValue args[] = {a1};
		out.clear();
		((LuaCFunctionTyped)cfunc.ptr)(args, 1, out);
		return out.empty() ? LuaValue() : std::move(out[0]);
	}
	if (callable.index() == INDEX_FUNCTION) {
		auto* func = callable.get<LuaCallable*>();
		return func->call1(out, a1);
	}
	const LuaValue args[] = {a1};
	call_lua_value(callable, args, 1, out);
	return out.empty() ? LuaValue() : std::move(out[0]);
}

inline LuaValue lua_call2(const LuaValue& callable, LuaValueVector& out, const LuaValue& a1, const LuaValue& a2) {
	if (callable.index() == INDEX_CFUNCTION) {
		auto cfunc = callable.get<LuaCFunction>();
		const LuaValue args[] = {a1, a2};
		out.clear();
		((LuaCFunctionTyped)cfunc.ptr)(args, 2, out);
		return out.empty() ? LuaValue() : std::move(out[0]);
	}
	if (callable.index() == INDEX_FUNCTION) {
		auto* func = callable.get<LuaCallable*>();
		return func->call2(out, a1, a2);
	}
	const LuaValue args[] = {a1, a2};
	call_lua_value(callable, args, 2, out);
	return out.empty() ? LuaValue() : std::move(out[0]);
}

inline LuaValue lua_call3(const LuaValue& callable, LuaValueVector& out, const LuaValue& a1, const LuaValue& a2, const LuaValue& a3) {
	if (callable.index() == INDEX_CFUNCTION) {
		auto cfunc = callable.get<LuaCFunction>();
		const LuaValue args[] = {a1, a2, a3};
		out.clear();
		((LuaCFunctionTyped)cfunc.ptr)(args, 3, out);
		return out.empty() ? LuaValue() : std::move(out[0]);
	}
	if (callable.index() == INDEX_FUNCTION) {
		auto* func = callable.get<LuaCallable*>();
		return func->call3(out, a1, a2, a3);
	}
	const LuaValue args[] = {a1, a2, a3};
	call_lua_value(callable, args, 3, out);
	return out.empty() ? LuaValue() : std::move(out[0]);
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

// Convenience overloads for call_lua_value
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
LuaValue lua_get_member(const LuaObject*& base, std::string_view key);
LuaValue lua_get_member(LuaObject* base, std::string_view key);
LuaValue lua_get_member(const LuaValue& base, long long key);
LuaValue lua_get_member(const LuaObject*& base, long long key);
LuaValue lua_get_member(LuaObject* base, long long key);
inline LuaValue lua_get_member(LuaObject* base, int key) { return lua_get_member(base, static_cast<long long>(key)); }

inline LuaValue lua_get_member(const LuaValue& base, const std::string& key) { return lua_get_member(base, std::string_view(key)); }
inline LuaValue lua_get_member(const LuaValue& base, const char* key) { return lua_get_member(base, std::string_view(key)); }

inline LuaValue lua_get_member(const LuaObject*& base, const std::string& key) { return lua_get_member(base, std::string_view(key)); }
inline LuaValue lua_get_member(const LuaObject*& base, const char* key) { return lua_get_member(base, std::string_view(key)); }

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
	const size_t idx = val.index();
	
	// Check for Nil (most common falsy)
	if (idx == INDEX_NIL) [[unlikely]] return false;
	
	// Check for Boolean
	if (idx == INDEX_BOOLEAN) [[unlikely]] {
		return val.get<bool>();
	}
	
	// Numbers, Strings, Tables, Functions are always truthy
	return true;
}

// Core call dispatch — fully inline for cross-TU inlining of fast paths
inline void call_lua_value(const LuaValue& callable, const LuaValue* args, size_t n_args,
						   LuaValueVector& out_result) {
	// 1. Reset the return buffer
	out_result.clear();

	// 2. Dispatch based on the Type Index
	switch (callable.index()) {
		
		// INDEX 7: LuaCallable* (Transpiled Lua Functions)
		case INDEX_FUNCTION: [[likely]] {
			const auto& func_ptr = callable.get<LuaCallable*>();
			// In a multithreaded engine, always verify the pointer isn't null
			if (func_ptr) [[likely]] {
				func_ptr->call(args, n_args, out_result);
				return;
			}
			break;
		}

		// INDEX 9: LuaCFunction (Native Library Functions)
		case INDEX_CFUNCTION: [[likely]] {
			const auto& cfunc = callable.get<LuaCFunction>();
			// Cast the raw pointer to the typed function and execute
			((LuaCFunctionTyped)(cfunc.ptr))(args, n_args, out_result);
			return;
		}

		// INDEX 6: LuaObject* (Tables with __call)
		case INDEX_OBJECT: [[unlikely]] {
			const auto& obj = callable.get<LuaObject*>();
			if (obj && obj->metatable) {
				LuaValue call_handler = obj->metatable->get_item("__call");
				if (is_lua_truthy(call_handler)) {
					// Optimization: Use a fast stack array for most calls, avoiding TLS and heap overhead.
					if (n_args <= 7) [[likely]] {
						LuaValue stack_args[8];
						stack_args[0] = callable;
						for (size_t i = 0; i < n_args; ++i) stack_args[i + 1] = args[i];
						call_lua_value(call_handler, stack_args, n_args + 1, out_result);
					} else {
						LuaValueVector heap_args;
						heap_args.reserve(n_args + 1);
						heap_args.push_back(callable); 
						for (size_t i = 0; i < n_args; ++i) heap_args.push_back(args[i]);
						call_lua_value(call_handler, heap_args.data(), heap_args.size(), out_result);
					}
					return;
				}
			}
			break; 
		}

		// INDEX 8: LuaCoroutine*
		case INDEX_COROUTINE: [[unlikely]] {
			// Usually handled via coroutine.resume, but if you want 
			// direct calling to work like resume, add logic here.
			break;
		}

		default: [[unlikely]]
			break;
	}

	// If we reach here, the value isn't a function, a C function, or a table with __call.
	[[unlikely]]
	throw std::runtime_error("attempt to call a " + get_lua_type_name(callable) + " value");
}

// ==========================================
// Inline Character Predicates for Tokenizer Optimization
// These avoid call_lua_value overhead for character checks
// ==========================================

inline char lua_get_char(const LuaValue& v) {
	switch (v.index()) {
		case INDEX_INTEGER:
			return static_cast<char>(v.get<long long>());
		case INDEX_DOUBLE:
			return static_cast<char>(v.get<double>());
		case INDEX_STRING: {
			const auto& s = v.get<std::string_view>();
			return s.size() == 1 ? s[0] : '\0';
		}
		case INDEX_STRING_VIEW: {
			const auto& sv = v.get<std::string_view>();
			return sv.size() == 1 ? sv[0] : '\0';
		}
		default:
			return '\0';
	}
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
	if (str.index() == INDEX_STRING || str.index() == INDEX_STRING_VIEW) {
		std::string_view sv = str.get<std::string_view>();
		if (i >= 1 && i <= static_cast<long long>(sv.size())) {
			return LuaObject::get_single_char(static_cast<unsigned char>(sv[i - 1]));
		}
	}
	static const LuaValue empty_str{std::string("")};
	return empty_str;
}

inline const LuaValue& lua_string_char_at(const LuaValue& str, const LuaValue& pos) {
	long long i = 0;
	if (pos.index() == INDEX_INTEGER || pos.index() == INDEX_DOUBLE) i = pos.get<long long>();
	else { static const LuaValue empty_str{std::string("")}; return empty_str; }
	return lua_string_char_at(str, i);
}

// Optimized Byte Access (replaces str:byte(i))
inline long long lua_string_byte_at_raw(const LuaValue& str, long long i) {
	if (str.index() == INDEX_STRING || str.index() == INDEX_STRING_VIEW) {
		std::string_view sv = str.get<std::string_view>();
		if (i >= 1 && i <= static_cast<long long>(sv.size())) {
			return static_cast<long long>(static_cast<unsigned char>(sv[i - 1]));
		}
	}
	return -1; // Indicate nil/out of bounds
}

inline long long lua_string_byte_at_raw(const LuaValue& str, const LuaValue& pos) {
	long long i = 0;
	if (pos.index() == INDEX_INTEGER || pos.index() == INDEX_DOUBLE) i = pos.get<long long>();
	else return -1;
	return lua_string_byte_at_raw(str, i);
}

inline LuaValue lua_string_byte_at(const LuaValue& str, long long i) {
	long long b = lua_string_byte_at_raw(str, i);
	if (b == -1) return LuaValue();
	return b;
}

inline LuaValue lua_string_byte_at(const LuaValue& str, const LuaValue& pos) {
	long long i = 0;
	if (pos.index() == INDEX_INTEGER || pos.index() == INDEX_DOUBLE) i = pos.get<long long>();
	else return LuaValue(); 
	return lua_string_byte_at(str, i);
}

// Optimized String View Overloads
inline bool lua_equals(const LuaValue& a, std::string_view b) {
	if (a.index() == INDEX_STRING || a.index() == INDEX_STRING_VIEW) return a.get<std::string_view>() == b;
	return false;
}
inline bool lua_equals(std::string_view a, const LuaValue& b) { return lua_equals(b, a); }
inline bool lua_equals(const LuaValue& a, const std::string& b) { return lua_equals(a, std::string_view(b)); }
inline bool lua_equals(const std::string& a, const LuaValue& b) { return lua_equals(b, std::string_view(a)); }
inline bool lua_equals(const LuaValue& a, const char* b) { return b ? lua_equals(a, std::string_view(b)) : a.is_nil(); }
inline bool lua_equals(const char* a, const LuaValue& b) { return a ? lua_equals(b, std::string_view(a)) : b.is_nil(); }

// Numeric Overloads to avoid ambiguity with literal 0 and improve performance
inline bool lua_equals(const LuaValue& a, double b) {
	if (a.index() == INDEX_DOUBLE || a.index() == INDEX_INTEGER) return a.get<double>() == b;
	return false;
}
inline bool lua_equals(double a, const LuaValue& b) { return lua_equals(b, a); }
inline bool lua_equals(const LuaValue& a, long long b) {
	if (a.index() == INDEX_INTEGER) return a.get<long long>() == b;
	if (a.index() == INDEX_DOUBLE) return a.get<double>() == static_cast<double>(b);
	return false;
}
inline bool lua_equals(long long a, const LuaValue& b) { return lua_equals(b, a); }
inline bool lua_equals(const LuaValue& a, int b) { return lua_equals(a, static_cast<long long>(b)); }
inline bool lua_equals(int a, const LuaValue& b) { return lua_equals(b, static_cast<long long>(a)); }

// Primitive overloads to resolve ambiguity when both sides are already unwrapped
inline bool lua_equals(long long a, long long b) { return a == b; }
inline bool lua_equals(double a, double b) { return a == b; }
inline bool lua_equals(long long a, double b) { return static_cast<double>(a) == b; }
inline bool lua_equals(double a, long long b) { return a == static_cast<double>(b); }
inline bool lua_equals(int a, int b) { return a == b; }

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
	if (a.index() == INDEX_STRING || a.index() == INDEX_STRING_VIEW) return a.get<std::string_view>() < b;
	return lua_less_than(a, LuaValue(b));
}
inline bool lua_less_than(std::string_view a, const LuaValue& b) {
	if (b.index() == INDEX_STRING || b.index() == INDEX_STRING_VIEW) return a < b.get<std::string_view>();
	return lua_less_than(LuaValue(a), b);
}
inline bool lua_less_than(const LuaValue& a, const std::string& b) { return lua_less_than(a, std::string_view(b)); }
inline bool lua_less_than(const std::string& a, const LuaValue& b) { return lua_less_than(std::string_view(a), b); }
inline bool lua_less_than(const LuaValue& a, const char* b) { return b ? lua_less_than(a, std::string_view(b)) : false; }
inline bool lua_less_than(const char* a, const LuaValue& b) { return a ? lua_less_than(std::string_view(a), b) : false; }

inline bool lua_less_than(const LuaValue& a, double b) {
	if (a.index() == INDEX_DOUBLE || a.index() == INDEX_INTEGER) return a.get<double>() < b;
	return false;
}
inline bool lua_less_than(double a, const LuaValue& b) {
	if (b.index() == INDEX_DOUBLE || b.index() == INDEX_INTEGER) return a < b.get<double>();
	return false;
}
inline bool lua_less_than(const LuaValue& a, long long b) {
	if (a.index() == INDEX_INTEGER) return a.get<long long>() < b;
	if (a.index() == INDEX_DOUBLE) return a.get<double>() < static_cast<double>(b);
	return false;
}
inline bool lua_less_than(long long a, const LuaValue& b) {
	if (b.index() == INDEX_INTEGER) return a < b.get<long long>();
	if (b.index() == INDEX_DOUBLE) return static_cast<double>(a) < b.get<double>();
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
	if (a.index() == INDEX_STRING) return a.get<std::string_view>() <= b;
	return lua_less_equals(a, LuaValue(b));
}
inline bool lua_less_equals(std::string_view a, const LuaValue& b) {
	if (b.index() == INDEX_STRING) return a <= b.get<std::string_view>();
	return lua_less_equals(LuaValue(a), b);
}
inline bool lua_less_equals(const LuaValue& a, const char* b) { return b ? lua_less_equals(a, std::string_view(b)) : false; }
inline bool lua_less_equals(const char* a, const LuaValue& b) { return a ? lua_less_equals(std::string_view(a), b) : false; }
inline bool lua_less_equals(const LuaValue& a, double b) {
	size_t idx = a.index();
	if (idx == INDEX_DOUBLE || idx == INDEX_INTEGER) return a.get<double>() <= b;
	return lua_less_equals(a, LuaValue(b));
}
inline bool lua_less_equals(double a, const LuaValue& b) {
	size_t idx = b.index();
	if (idx == INDEX_DOUBLE || idx == INDEX_INTEGER) return a <= b.get<double>();
	return lua_less_equals(LuaValue(a), b);
}
inline bool lua_less_equals(const LuaValue& a, long long b) {
	size_t idx = a.index();
	if (idx == INDEX_INTEGER) return a.get<long long>() <= b;
	if (idx == INDEX_DOUBLE) return a.get<double>() <= static_cast<double>(b);
	return lua_less_equals(a, LuaValue(b));
}
inline bool lua_less_equals(long long a, const LuaValue& b) {
	size_t idx = b.index();
	if (idx == INDEX_INTEGER) return a <= b.get<long long>();
	if (idx == INDEX_DOUBLE) return static_cast<double>(a) <= b.get<double>();
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
LuaValue lua_concat(const LuaValue& a, LuaValue&& b); 
LuaValue lua_concat(LuaValue&& a, LuaValue&& b);

template <typename T1, typename T2, typename T3, typename... Ts>
LuaValue lua_concat(T1&& a, T2&& b, T3&& c, Ts&&... rest) {
	// Right-associative: a .. (b .. (c ...))
	return lua_concat(
		std::forward<T1>(a), 
		lua_concat(std::forward<T2>(b), std::forward<T3>(c), std::forward<Ts>(rest)...)
	);
}

LuaValue as_view(const LuaValue& v);

extern LuaObject* _G;

inline double to_double(const LuaValue& v) {
	size_t idx = v.index();
	if (idx == INDEX_DOUBLE || idx == INDEX_INTEGER) return v.get<double>();
	if (idx == INDEX_STRING) {
		char* end;
		std::string s(v.get<std::string_view>());
		double res = std::strtod(s.c_str(), &end);
		if (end != s.c_str()) return res;
	}
	return 0.0;
}

inline bool is_integer(const LuaValue& v) {
	return v.index() == INDEX_INTEGER;
}

inline LuaValue operator-(const LuaValue& a) {
	if (a.index() == INDEX_INTEGER) [[likely]] return -a.get<long long>();
	if (a.index() == INDEX_DOUBLE) [[likely]] return -a.get<double>();
	return -to_double(a);
}

inline LuaValue operator+(const LuaValue& a, const LuaValue& b) {
	// Combined type key for single-branch dispatch
	const uint32_t dispatch = (static_cast<uint32_t>(a.index()) << 8) | static_cast<uint32_t>(b.index());

	switch (dispatch) {
		case (INDEX_INTEGER << 8) | INDEX_INTEGER:
			return a.get<long long>() + b.get<long long>();
		
		case (INDEX_INTEGER << 8) | INDEX_DOUBLE:
			return static_cast<double>(a.get<long long>()) + b.get<double>();
			
		case (INDEX_DOUBLE << 8) | INDEX_INTEGER:
			return a.get<double>() + static_cast<double>(b.get<long long>());
			
		case (INDEX_DOUBLE << 8) | INDEX_DOUBLE:
			return a.get<double>() + b.get<double>();

		default: [[unlikely]]
			return to_double(a) + to_double(b);
	}
}
// ... repeat pattern for - and * ...

inline LuaValue operator+(const LuaValue& a, double b) { return to_double(a) + b; }
inline LuaValue operator+(double a, const LuaValue& b) { return a + to_double(b); }
inline LuaValue operator+(const LuaValue& a, long long b) {
	if (a.index() == INDEX_INTEGER) [[likely]] return a.get<long long>() + b;
	return to_double(a) + static_cast<double>(b);
}
inline LuaValue operator+(long long a, const LuaValue& b) {
	if (b.index() == INDEX_INTEGER) [[likely]] return a + b.get<long long>();
	return static_cast<double>(a) + to_double(b);
}
inline LuaValue operator+(const LuaValue& a, int b) { return a + static_cast<long long>(b); }
inline LuaValue operator+(int a, const LuaValue& b) { return static_cast<long long>(a) + b; }

inline LuaValue operator-(const LuaValue& a, const LuaValue& b) {
	// Combined type key for single-branch dispatch
	const uint32_t dispatch = (static_cast<uint32_t>(a.index()) << 8) | static_cast<uint32_t>(b.index());

	switch (dispatch) {
		case (INDEX_INTEGER << 8) | INDEX_INTEGER:
			return a.get<long long>() - b.get<long long>();
		
		case (INDEX_INTEGER << 8) | INDEX_DOUBLE:
			return static_cast<double>(a.get<long long>()) - b.get<double>();
			
		case (INDEX_DOUBLE << 8) | INDEX_INTEGER:
			return a.get<double>() - static_cast<double>(b.get<long long>());
			
		case (INDEX_DOUBLE << 8) | INDEX_DOUBLE:
			return a.get<double>() - b.get<double>();

		default: [[unlikely]]
			return to_double(a) - to_double(b);
	}
}

inline LuaValue operator-(const LuaValue& a, double b) { return to_double(a) - b; }
inline LuaValue operator-(double a, const LuaValue& b) { return a - to_double(b); }
inline LuaValue operator-(const LuaValue& a, long long b) {
	if (a.index() == INDEX_INTEGER) [[likely]] return a.get<long long>() - b;
	return to_double(a) - static_cast<double>(b);
}
inline LuaValue operator-(long long a, const LuaValue& b) {
	if (b.index() == INDEX_INTEGER) [[likely]] return a - b.get<long long>();
	return static_cast<double>(a) - to_double(b);
}

inline LuaValue operator*(const LuaValue& a, const LuaValue& b) {
	// Combined type key for single-branch dispatch
	const uint32_t dispatch = (static_cast<uint32_t>(a.index()) << 8) | static_cast<uint32_t>(b.index());

	switch (dispatch) {
		case (INDEX_INTEGER << 8) | INDEX_INTEGER:
			return a.get<long long>() * b.get<long long>();
		
		case (INDEX_INTEGER << 8) | INDEX_DOUBLE:
			return static_cast<double>(a.get<long long>()) * b.get<double>();
			
		case (INDEX_DOUBLE << 8) | INDEX_INTEGER:
			return a.get<double>() * static_cast<double>(b.get<long long>());
			
		case (INDEX_DOUBLE << 8) | INDEX_DOUBLE:
			return a.get<double>() * b.get<double>();

		default: [[unlikely]]
			return to_double(a) * to_double(b);
	}
}

inline LuaValue operator*(const LuaValue& a, double b) { return to_double(a) * b; }
inline LuaValue operator*(double a, const LuaValue& b) { return a * to_double(b); }
inline LuaValue operator*(const LuaValue& a, long long b) {
	if (a.index() == INDEX_INTEGER) [[likely]] return a.get<long long>() * b;
	return to_double(a) * static_cast<double>(b);
}
inline LuaValue operator*(long long a, const LuaValue& b) {
	if (b.index() == INDEX_INTEGER) [[likely]] return a * b.get<long long>();
	return static_cast<double>(a) * to_double(b);
}

inline LuaValue operator/(const LuaValue& a, const LuaValue& b) {
	return to_double(a) / to_double(b);
}
inline LuaValue operator/(const LuaValue& a, double b) { return to_double(a) / b; }
inline LuaValue operator/(double a, const LuaValue& b) { return a / to_double(b); }

inline LuaValue operator%(const LuaValue& a, const LuaValue& b) {
	if (is_integer(a) && is_integer(b)) {
		long long av = a.get<long long>();
		long long bv = b.get<long long>();
		if (bv == 0) return 0.0;
		return av % bv;
	}
	return std::fmod(to_double(a), to_double(b));
}

inline LuaValue operator~(const LuaValue& a) {
	if (a.index() == INDEX_INTEGER) return ~(a.get<long long>());
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
	if (val.index() == INDEX_STRING) return static_cast<long long>(val.get<std::string_view>().length());
	if (val.index() == INDEX_OBJECT) {
		auto* obj = val.get<LuaObject*>();
		if (obj->metatable) {
			auto len_meta = obj->metatable->get_item("__len");
			if (!len_meta.is_nil()) {
				LuaValueVector res;
				call_lua_value(len_meta, &val, 1, res);
				return res.empty() ? LuaValue() : res[0];
			}
		}
		return static_cast<long long>(obj->array_part.size());
	}
	throw std::runtime_error("attempt to get length of a " + get_lua_type_name(val) + " value");
}

inline long long lua_get_length_int(const LuaValue& val) {
	if (val.index() == INDEX_STRING) return static_cast<long long>(val.get<std::string_view>().length());
	if (val.index() == INDEX_OBJECT) {
		auto* obj = val.get<LuaObject*>();
		if (obj->metatable) {
			auto len_meta = obj->metatable->get_item("__len");
			if (!len_meta.is_nil()) {
				LuaValueVector res;
				call_lua_value(len_meta, &val, 1, res);
				LuaValue ret = res.empty() ? LuaValue() : res[0];
				return static_cast<long long>(to_double(ret));
			}
		}
		return static_cast<long long>(obj->array_part.size());
	}
	throw std::runtime_error("attempt to get length of a " + get_lua_type_name(val) + " value");
}

inline LuaValue lua_get_member(const LuaValue& base, const LuaValue& key) {
	// 1. TABLE ACCESS (The 99% case)
	if (base.index() == INDEX_OBJECT) [[likely]] {
		return base.get<LuaObject*>()->get_item(key);
	}

	if (base.index() == INDEX_STRING) [[unlikely]] {
		static LuaObject* string_lib_obj = _G->get_item("string").get<LuaObject*>();
		return string_lib_obj->get_item(key);
	}

	[[unlikely]]
	throw std::runtime_error("attempt to index a " + get_lua_type_name(base) + " value");
}

inline LuaValue lua_get_member(const LuaValue& base, std::string_view key) {
	if (base.index() == INDEX_OBJECT) [[likely]] {
		return base.get<LuaObject*>()->get_item(key);
	}

	if (base.index() == INDEX_STRING) [[unlikely]] {
		static LuaObject* string_lib_obj = _G->get_item("string").get<LuaObject*>();
		return string_lib_obj->get_item(key);
	}

	[[unlikely]]
	throw std::runtime_error("attempt to index a " + get_lua_type_name(base) + " value");
}

inline LuaValue lua_get_member(LuaObject* base, std::string_view key) {
	if (base) [[likely]] {
		return base->get_item(key);
	}
	return LuaValue();
}

inline LuaValue lua_get_member(const LuaValue& base, long long key) {
	if (base.index() == INDEX_OBJECT) [[likely]] {
		return base.get<LuaObject*>()->get_item(key);
	}
	return LuaValue();
}

inline LuaValue get_return_value(LuaValueVector& results, size_t index) {
	if (index < results.size()) [[likely]] {
		return std::move(results[index]);
	} else {
		return LuaValue();
	}
}

inline LuaValue get_return_value(LuaValueVector&& results, size_t index) {
	if (index < results.size()) [[likely]] {
		return std::move(results[index]);
	} else {
		return LuaValue();
	}
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

LuaValueVector& luax_get_ret_buf();
void luax_release_ret_buf();

struct LuaRetBufGuard {
	LuaValueVector& buf;
	LuaRetBufGuard() : buf(luax_get_ret_buf()) {}
	~LuaRetBufGuard() { luax_release_ret_buf(); }
};

void luax_flush_thread_pool();

template <> inline std::string_view LuaValue::get<std::string_view>() const {
    if ((data & TAG_MASK) == TAG_STRING) {
        return reinterpret_cast<LuaString*>(data & PAYLOAD_MASK)->str;
    }
    return "";
}
template <> inline std::string LuaValue::get<std::string>() const {
    return std::string(get<std::string_view>());
}
template <> inline LuaObject* LuaValue::get<LuaObject*>() const {
    return reinterpret_cast<LuaObject*>(data & PAYLOAD_MASK);
}
template <> inline LuaCallable* LuaValue::get<LuaCallable*>() const {
    return reinterpret_cast<LuaCallable*>(data & PAYLOAD_MASK);
}
template <> inline LuaCoroutine* LuaValue::get<LuaCoroutine*>() const {
    return reinterpret_cast<LuaCoroutine*>(data & PAYLOAD_MASK);
}
template <> inline LuaCFunction LuaValue::get<LuaCFunction>() const {
    return LuaCFunction{reinterpret_cast<LuaCFunctionPtr>(data & PAYLOAD_MASK)};
}

#endif // LUA_OBJECT_HPP
