#include "debug.hpp"
#include "lua_object.hpp"
#include <stdexcept>

std::vector<LuaValue> debug_debug(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.debug is not supported in the translated environment.");
}

std::vector<LuaValue> debug_gethook(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.gethook is not supported in the translated environment.");
}

std::vector<LuaValue> debug_getinfo(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.getinfo is not supported in the translated environment.");
}

std::vector<LuaValue> debug_getlocal(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.getlocal is not supported in the translated environment.");
}

std::vector<LuaValue> debug_getmetatable(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.getmetatable is not supported in the translated environment.");
}

std::vector<LuaValue> debug_getregistry(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.getregistry is not supported in the translated environment.");
}

std::vector<LuaValue> debug_getupvalue(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.getupvalue is not supported in the translated environment.");
}

std::vector<LuaValue> debug_getuservalue(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.getuservalue is not supported in the translated environment.");
}

std::vector<LuaValue> debug_sethook(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.sethook is not supported in the translated environment.");
}

std::vector<LuaValue> debug_setlocal(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.setlocal is not supported in the translated environment.");
}

std::vector<LuaValue> debug_setmetatable(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.setmetatable is not supported in the translated environment.");
}

std::vector<LuaValue> debug_setupvalue(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.setupvalue is not supported in the translated environment.");
}

std::vector<LuaValue> debug_setuservalue(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.setuservalue is not supported in the translated environment.");
}

std::vector<LuaValue> debug_traceback(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.traceback is not supported in the translated environment.");
}

std::vector<LuaValue> debug_upvalueid(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.upvalueid is not supported in the translated environment.");
}

std::vector<LuaValue> debug_upvaluejoin(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("debug.upvaluejoin is not supported in the translated environment.");
}

std::shared_ptr<LuaObject> create_debug_library() {
    auto debug_lib = std::make_shared<LuaObject>();

    debug_lib->set("debug", std::make_shared<LuaFunctionWrapper>(debug_debug));
    debug_lib->set("gethook", std::make_shared<LuaFunctionWrapper>(debug_gethook));
    debug_lib->set("getinfo", std::make_shared<LuaFunctionWrapper>(debug_getinfo));
    debug_lib->set("getlocal", std::make_shared<LuaFunctionWrapper>(debug_getlocal));
    debug_lib->set("getmetatable", std::make_shared<LuaFunctionWrapper>(debug_getmetatable));
    debug_lib->set("getregistry", std::make_shared<LuaFunctionWrapper>(debug_getregistry));
    debug_lib->set("getupvalue", std::make_shared<LuaFunctionWrapper>(debug_getupvalue));
    debug_lib->set("getuservalue", std::make_shared<LuaFunctionWrapper>(debug_getuservalue));
    debug_lib->set("sethook", std::make_shared<LuaFunctionWrapper>(debug_sethook));
    debug_lib->set("setlocal", std::make_shared<LuaFunctionWrapper>(debug_setlocal));
    debug_lib->set("setmetatable", std::make_shared<LuaFunctionWrapper>(debug_setmetatable));
    debug_lib->set("setupvalue", std::make_shared<LuaFunctionWrapper>(debug_setupvalue));
    debug_lib->set("setuservalue", std::make_shared<LuaFunctionWrapper>(debug_setuservalue));
    debug_lib->set("traceback", std::make_shared<LuaFunctionWrapper>(debug_traceback));
    debug_lib->set("upvalueid", std::make_shared<LuaFunctionWrapper>(debug_upvalueid));
    debug_lib->set("upvaluejoin", std::make_shared<LuaFunctionWrapper>(debug_upvaluejoin));

    return debug_lib;
}