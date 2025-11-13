#ifndef TABLE_HPP
#define TABLE_HPP

#include "lua_object.hpp"
#include <memory>
#include <vector>

std::shared_ptr<LuaObject> create_table_library();

// Declarations for generic for loop iterators
std::vector<LuaValue> pairs_iterator(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_pairs(std::shared_ptr<LuaObject> args);

std::vector<LuaValue> ipairs_iterator(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> lua_ipairs(std::shared_ptr<LuaObject> args);

#endif // TABLE_HPP
