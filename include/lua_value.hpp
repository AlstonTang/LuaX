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
struct LuaValueHash {
	using is_transparent = void;

	size_t operator()(const LuaValue& v) const {
		switch (v.index()) {
			case INDEX_NIL: return 0;
			case INDEX_BOOLEAN: return std::hash<bool>{}(std::get<bool>(v));
			case INDEX_DOUBLE: return std::hash<double>{}(std::get<double>(v));
			case INDEX_INTEGER: return std::hash<long long>{}(std::get<long long>(v));
			case INDEX_STRING: return std::hash<std::string_view>{}(std::get<std::string>(v));
			case INDEX_STRING_VIEW: return std::hash<std::string_view>{}(std::get<std::string_view>(v));
			case INDEX_OBJECT: return std::hash<void*>{}(std::get<std::shared_ptr<LuaObject>>(v).get());
			case INDEX_FUNCTION: return std::hash<void*>{}(std::get<std::shared_ptr<LuaFunctionWrapper>>(v).get());
			case INDEX_COROUTINE: return std::hash<void*>{}(std::get<std::shared_ptr<LuaCoroutine>>(v).get());
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
		if (lhs.index() == INDEX_STRING_VIEW && rhs.index() == INDEX_STRING_VIEW) {
			auto sv1 = std::get<INDEX_STRING_VIEW>(lhs);
			auto sv2 = std::get<INDEX_STRING_VIEW>(rhs);
			if (sv1.data() == sv2.data() && sv1.size() == sv2.size()) [[likely]] return true;
			return sv1 == sv2;
		}
		if (lhs.index() == INDEX_STRING && rhs.index() == INDEX_STRING_VIEW) {
			return std::get<std::string>(lhs) == std::get<std::string_view>(rhs);
		}
		if (lhs.index() == INDEX_STRING_VIEW && rhs.index() == INDEX_STRING) {
			return std::get<std::string_view>(lhs) == std::get<std::string>(rhs);
		}
		return lhs == rhs;
	}

	bool operator()(const LuaValue& lhs, std::string_view rhs) const {
		if (lhs.index() == INDEX_STRING_VIEW) {
			auto sv = std::get<INDEX_STRING_VIEW>(lhs);
			if (sv.data() == rhs.data() && sv.size() == rhs.size()) [[likely]] return true;
			return sv == rhs;
		}
		if (lhs.index() == INDEX_STRING) {
			return std::get<INDEX_STRING>(lhs) == rhs;
		}
		return false;
	}

	bool operator()(std::string_view lhs, const LuaValue& rhs) const {
		if (rhs.index() == INDEX_STRING_VIEW) {
			auto sv = std::get<INDEX_STRING_VIEW>(rhs);
			if (sv.data() == lhs.data() && sv.size() == lhs.size()) [[likely]] return true;
			return lhs == sv;
		}
		if (rhs.index() == INDEX_STRING) {
			return lhs == std::get<INDEX_STRING>(rhs);
		}
		return false;
	}

	bool operator()(const LuaValue& lhs, const char* rhs) const {
		return (*this)(lhs, std::string_view(rhs));
	}

	bool operator()(const char* lhs, const LuaValue& rhs) const {
		return (*this)(std::string_view(lhs), rhs);
	}
};

#include "pool_allocator.hpp"

// Define a standardized vector type using the pool allocator
// This allows all LuaValue vectors (args, returns, props) to share the thread-local pool.
using LuaValueVector = std::vector<LuaValue, PoolAllocator<LuaValue>>;

#endif // LUA_VALUE_HPP
