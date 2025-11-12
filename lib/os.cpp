#include "os.hpp"
#include "lua_object.hpp"
#include <chrono>
#include <ctime>

// os.clock
LuaValue os_clock(std::shared_ptr<LuaObject> args) {
    return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
}

// os.time
LuaValue os_time(std::shared_ptr<LuaObject> args) {
    return static_cast<double>(std::time(nullptr));
}

std::shared_ptr<LuaObject> create_os_library() {
    auto os_lib = std::make_shared<LuaObject>();

    os_lib->set("clock", std::make_shared<LuaFunctionWrapper>(os_clock));
    os_lib->set("time", std::make_shared<LuaFunctionWrapper>(os_time));

    return os_lib;
}
