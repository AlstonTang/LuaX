#ifndef DEBUG_HPP
#define DEBUG_HPP

#include "lua_object.hpp"
#include <memory>

std::shared_ptr<LuaObject> create_debug_library();

#endif // DEBUG_HPP