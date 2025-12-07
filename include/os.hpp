#ifndef OS_HPP
#define OS_HPP

#include "lua_object.hpp"
#include <memory>

std::shared_ptr<LuaObject> create_os_library();

#endif // OS_HPP