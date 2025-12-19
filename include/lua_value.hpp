#ifndef LUA_VALUE_HPP
#define LUA_VALUE_HPP

#include <variant>
#include <string>
#include <memory> // Required for std::shared_ptr

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
	INDEX_OBJECT = 5,
	INDEX_FUNCTION = 6,
	INDEX_COROUTINE = 7
};

#endif // LUA_VALUE_HPP
