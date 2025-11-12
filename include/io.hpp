#ifndef IO_HPP
#define IO_HPP

#include "lua_object.hpp"
#include <memory>

std::shared_ptr<LuaObject> create_io_library();

#endif // IO_HPP
