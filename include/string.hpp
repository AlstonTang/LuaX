#ifndef STRING_HPP
#define STRING_HPP

#include "lua_object.hpp"
#include <memory>

std::shared_ptr<LuaObject> create_string_library();

#endif // STRING_HPP
