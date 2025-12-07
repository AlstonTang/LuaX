#ifndef COROUTINE_HPP
#define COROUTINE_HPP

#include "lua_object.hpp"
#include <memory>

std::shared_ptr<LuaObject> create_coroutine_library();

#endif // COROUTINE_HPP