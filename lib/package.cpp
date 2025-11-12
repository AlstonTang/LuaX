#include "package.hpp"
#include "lua_object.hpp"
#include <string>

// package.path
LuaValue package_path(std::shared_ptr<LuaObject> args) {
    return LuaValue(std::string("")); // For now, return an empty string
}

// package.cpath
LuaValue package_cpath(std::shared_ptr<LuaObject> args) {
    return LuaValue(std::string("")); // For now, return an empty string
}

std::shared_ptr<LuaObject> create_package_library() {
    auto package_lib = std::make_shared<LuaObject>();

    package_lib->set("path", LuaValue(std::string("")));
    package_lib->set("cpath", LuaValue(std::string("")));

    return package_lib;
}
