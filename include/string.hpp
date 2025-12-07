#ifndef STRING_HPP
#define STRING_HPP

#include "lua_object.hpp"
#include "lua_value.hpp" // Include LuaValue
#include <memory>

std::shared_ptr<LuaObject> create_string_library();

std::vector<LuaValue> lua_string_match(const LuaValue& str, const LuaValue& pattern);
std::vector<LuaValue> lua_string_find(const LuaValue& str, const LuaValue& pattern);
std::vector<LuaValue> lua_string_gsub(const LuaValue& str, const LuaValue& pattern, const LuaValue& replacement);

#endif // STRING_HPP