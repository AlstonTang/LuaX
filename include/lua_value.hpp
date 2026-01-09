#ifndef LUA_VALUE_HPP
#define LUA_VALUE_HPP

#include <variant>
#include <string>
#include <string_view>
#include <memory>

// Forward declarations for types used in LuaValue
class LuaObject;
struct LuaFunctionWrapper;
class LuaCoroutine;

// Define LuaValue using forward declarations for recursive types
using LuaValue = std::variant<
	std::monostate, // for nil
	bool,
	double,
	long long,
	std::string,
	std::string_view,
	std::shared_ptr<LuaObject>,
	std::shared_ptr<LuaFunctionWrapper>,
	std::shared_ptr<LuaCoroutine>
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
	INDEX_COROUTINE = 8
};

// Custom hasher for LuaValue to support heterogeneous lookup with string_view
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
        size_t lidx = lhs.index();
        size_t ridx = rhs.index();
        
        if (lidx == ridx) {
            if (lidx == INDEX_STRING_VIEW) {
                auto sv1 = std::get<INDEX_STRING_VIEW>(lhs);
                auto sv2 = std::get<INDEX_STRING_VIEW>(rhs);
                if (sv1.data() == sv2.data() && sv1.size() == sv2.size()) [[likely]] return true;
                return sv1 == sv2;
            }
            return lhs == rhs;
        }

        // Cross-type string comparison
        if (lidx == INDEX_STRING && ridx == INDEX_STRING_VIEW)
            return std::get<INDEX_STRING>(lhs) == std::get<INDEX_STRING_VIEW>(rhs);
        if (lidx == INDEX_STRING_VIEW && ridx == INDEX_STRING)
            return std::get<INDEX_STRING_VIEW>(lhs) == std::get<INDEX_STRING>(rhs);

        // Cross-type numeric comparison
        if ((lidx == INDEX_DOUBLE || lidx == INDEX_INTEGER) && 
            (ridx == INDEX_DOUBLE || ridx == INDEX_INTEGER)) {
            double d1 = (lidx == INDEX_DOUBLE) ? std::get<double>(lhs) : (double)std::get<long long>(lhs);
            double d2 = (ridx == INDEX_DOUBLE) ? std::get<double>(rhs) : (double)std::get<long long>(rhs);
            return d1 == d2;
        }

        return false;
	}

	bool operator()(const LuaValue& lhs, std::string_view rhs) const {
        size_t idx = lhs.index();
		if (idx == INDEX_STRING_VIEW) {
			auto sv = std::get<INDEX_STRING_VIEW>(lhs);
			if (sv.data() == rhs.data() && sv.size() == rhs.size()) [[likely]] return true;
			return sv == rhs;
		}
		if (idx == INDEX_STRING) {
			return std::get<INDEX_STRING>(lhs) == rhs;
		}
		return false;
	}

	bool operator()(std::string_view lhs, const LuaValue& rhs) const {
		return (*this)(rhs, lhs);
	}

	bool operator()(const LuaValue& lhs, const char* rhs) const {
		return (*this)(lhs, std::string_view(rhs));
	}

	bool operator()(const char* lhs, const LuaValue& rhs) const {
		return (*this)(rhs, std::string_view(lhs));
	}
};

#include "pool_allocator.hpp"

// Define a standardized vector type using the pool allocator
// This allows all LuaValue vectors (args, returns, props) to share the thread-local pool.
using LuaValueVector = std::vector<LuaValue, PoolAllocator<LuaValue>>;

#endif // LUA_VALUE_HPP
