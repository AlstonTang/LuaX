#ifndef PACKAGE_HPP
#define PACKAGE_HPP

#include "lua_object.hpp"
#include <memory>

std::shared_ptr<LuaObject> create_package_library();

#endif // PACKAGE_HPP