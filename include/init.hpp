#ifndef INIT_HPP
#define INIT_HPP

#include "lua_object.hpp"
#include "math.hpp"
#include "io.hpp"
#include "string.hpp"
#include "os.hpp"
#include "package.hpp"
#include "table.hpp"
#include "utf8.hpp"

void init_G(int argc, char* argv[]);

std::shared_ptr<LuaObject> create_package_library();
std::shared_ptr<LuaObject> create_utf8_library();
std::shared_ptr<LuaObject> create_coroutine_library();
std::shared_ptr<LuaObject> create_debug_library();

#endif // INIT_HPP
