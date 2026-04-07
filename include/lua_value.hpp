#ifndef LUA_VALUE_HPP
#define LUA_VALUE_HPP

#include <variant>
#include <string>
#include <string_view>
#include <memory>

// Forward declarations for types used in LuaValue
class LuaObject;
struct LuaCallable;
class LuaCoroutine;

// Fast C function dispatch type (Uses void* to avoid circular dependency)
typedef void (*LuaCFunctionPtr)(const void*, size_t, void*);
struct LuaCFunction {
    LuaCFunctionPtr ptr;
    bool operator==(const LuaCFunction& other) const { return ptr == other.ptr; }
};

#define LUA_C_FUNC(f) LuaCFunction{(LuaCFunctionPtr)(f)}

// Define LuaValue using forward declarations for recursive types
using LuaValue = std::variant<
	std::monostate, // for nil
	bool,
	double,
	long long,
	std::string,
	std::string_view,
	std::shared_ptr<LuaObject>,
	std::shared_ptr<LuaCallable>,
	std::shared_ptr<LuaCoroutine>,
	LuaCFunction // Raw C function pointer (Fast path)
>;

enum LuaTypeIndex {
	INDEX_NIL = 0,
	INDEX_BOOLEAN = 1,
	INDEX_DOUBLE = 2,
	INDEX_INTEGER = 3,
	INDEX_STRING = 4,
	INDEX_STRING_VIEW = 5,
	INDEX_OBJECT = 6,
	INDEX_FUNCTION = 7,
	INDEX_COROUTINE = 8,
	INDEX_CFUNCTION = 9
};

// Custom hasher for LuaValue to support heterogeneous lookup with string_view
struct LuaValueHash {
	using is_transparent = void;

	size_t operator()(const LuaValue& v) const {
        size_t idx = v.index();
        switch (idx) {
			case INDEX_NIL: return 0;
			case INDEX_BOOLEAN: return std::hash<bool>{}(std::get<bool>(v));
			case INDEX_DOUBLE: 
            case INDEX_INTEGER: {
                double d = (idx == INDEX_DOUBLE) ? std::get<double>(v) : static_cast<double>(std::get<long long>(v));
                return std::hash<double>{}(d);
            }
			case INDEX_STRING: return std::hash<std::string_view>{}(std::get<std::string>(v));
			case INDEX_STRING_VIEW: return std::hash<std::string_view>{}(std::get<std::string_view>(v));
			case INDEX_OBJECT: return std::hash<void*>{}(std::get<INDEX_OBJECT>(v).get());
			case INDEX_FUNCTION: return std::hash<void*>{}(std::get<INDEX_FUNCTION>(v).get());
			case INDEX_COROUTINE: return std::hash<void*>{}(std::get<INDEX_COROUTINE>(v).get());
			case INDEX_CFUNCTION: return std::hash<void*>{}((void*)std::get<INDEX_CFUNCTION>(v).ptr);
			default: return 0;
		}
	}

	size_t operator()(std::string_view sv) const {
		return std::hash<std::string_view>{}(sv);
	}

	size_t operator()(const char* s) const {
		return std::hash<std::string_view>{}(s);
	}
};

// Custom equality for LuaValue to support heterogeneous lookup
struct LuaValueEq {
    using is_transparent = void;

    bool operator()(const LuaValue& lhs, const LuaValue& rhs) const {
        const size_t lidx = lhs.index();
        const size_t ridx = rhs.index();

        // 1. FAST PATH: Same internal type
        if (lidx == ridx) [[likely]] {
            // Hot Path: String Views (Interned members)
            if (lidx == INDEX_STRING_VIEW) [[likely]] {
                const auto sv1 = std::get<INDEX_STRING_VIEW>(lhs);
                const auto sv2 = std::get<INDEX_STRING_VIEW>(rhs);
                
                // POINTER EQUALITY: The absolute fastest check for interned strings.
                if (sv1.data() == sv2.data()) [[likely]] {
                    // In a perfect interned world, data match implies size match, 
                    // but we check size to support substrings/views.
                    return sv1.size() == sv2.size();
                }
                // Fallback for non-interned views
                return sv1 == sv2;
            }
            
            // Hot Path: Tables/Objects
            if (lidx == INDEX_OBJECT) [[likely]] {
                return std::get<INDEX_OBJECT>(lhs) == std::get<INDEX_OBJECT>(rhs);
            }

            // Primitive types (bool, double, int, monostate)
            return lhs == rhs;
        }

        // 2. CROSS-TYPE COMPARISONS (Cold Path)

        // String vs View (Content comparison)
        if ((lidx == INDEX_STRING && ridx == INDEX_STRING_VIEW) || 
            (lidx == INDEX_STRING_VIEW && ridx == INDEX_STRING)) [[unlikely]] {
            std::string_view sv1 = (lidx == INDEX_STRING) ? std::get<INDEX_STRING>(lhs) : std::get<INDEX_STRING_VIEW>(lhs);
            std::string_view sv2 = (ridx == INDEX_STRING) ? std::get<INDEX_STRING>(rhs) : std::get<INDEX_STRING_VIEW>(rhs);
            return sv1 == sv2;
        }

        // Numeric Comparison (Integer vs Double)
        // Lua Semantics: 1 == 1.0 is true
        if ((lidx == INDEX_DOUBLE || lidx == INDEX_INTEGER) && 
            (ridx == INDEX_DOUBLE || ridx == INDEX_INTEGER)) [[unlikely]] {
            double d1 = (lidx == INDEX_DOUBLE) ? std::get<double>(lhs) : static_cast<double>(std::get<long long>(lhs));
            double d2 = (ridx == INDEX_DOUBLE) ? std::get<double>(rhs) : static_cast<double>(std::get<long long>(rhs));
            return d1 == d2;
        }

        return false;
    }

    // Specialized for map lookups using string literals (e.g. properties->find("name"))
    bool operator()(const LuaValue& lhs, std::string_view rhs) const {
        const size_t idx = lhs.index();

        if (idx == INDEX_STRING_VIEW) [[likely]] {
            const auto sv = std::get<INDEX_STRING_VIEW>(lhs);
            // Pointer match for interned keys
            if (sv.data() == rhs.data()) [[likely]] return sv.size() == rhs.size();
            return sv == rhs;
        }

        if (idx == INDEX_STRING) [[unlikely]] {
            return std::get<INDEX_STRING>(lhs) == rhs;
        }

        return false;
    }

    // Symmetry overloads for transparency
    bool operator()(std::string_view lhs, const LuaValue& rhs) const { return (*this)(rhs, lhs); }
    bool operator()(const LuaValue& lhs, const char* rhs) const { return (*this)(lhs, std::string_view(rhs)); }
    bool operator()(const char* lhs, const LuaValue& rhs) const { return (*this)(rhs, std::string_view(lhs)); }
};

#include "pool_allocator.hpp"
#include <vector>

// Define a standardized vector type using the pool allocator
// This allows all LuaValue vectors (args, returns, props) to share the thread-local pool.
using LuaValueVector = std::vector<LuaValue, PoolAllocator<LuaValue>>;

// Typed version for internal use
typedef void (*LuaCFunctionTyped)(const LuaValue*, size_t, LuaValueVector&);

#endif // LUA_VALUE_HPP
