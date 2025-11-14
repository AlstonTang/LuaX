#include "coroutine.hpp"
#include "lua_object.hpp"
#include <stdexcept>

std::vector<LuaValue> coroutine_create(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("coroutine.create is not supported in the translated environment.");
}

std::vector<LuaValue> coroutine_resume(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("coroutine.resume is not supported in the translated environment.");
}

std::vector<LuaValue> coroutine_yield(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("coroutine.yield is not supported in the translated environment.");
}

std::vector<LuaValue> coroutine_status(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("coroutine.status is not supported in the translated environment.");
}

std::vector<LuaValue> coroutine_running(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("coroutine.running is not supported in the translated environment.");
}

std::vector<LuaValue> coroutine_wrap(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("coroutine.wrap is not supported in the translated environment.");
}

std::vector<LuaValue> coroutine_isyieldable(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("coroutine.isyieldable is not supported in the translated environment.");
}

std::vector<LuaValue> coroutine_close(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("coroutine.close is not supported in the translated environment.");
}

std::shared_ptr<LuaObject> create_coroutine_library() {
    auto coroutine_lib = std::make_shared<LuaObject>();

    coroutine_lib->set("create", std::make_shared<LuaFunctionWrapper>(coroutine_create));
    coroutine_lib->set("resume", std::make_shared<LuaFunctionWrapper>(coroutine_resume));
    coroutine_lib->set("yield", std::make_shared<LuaFunctionWrapper>(coroutine_yield));
    coroutine_lib->set("status", std::make_shared<LuaFunctionWrapper>(coroutine_status));
    coroutine_lib->set("running", std::make_shared<LuaFunctionWrapper>(coroutine_running));
    coroutine_lib->set("wrap", std::make_shared<LuaFunctionWrapper>(coroutine_wrap));
    coroutine_lib->set("isyieldable", std::make_shared<LuaFunctionWrapper>(coroutine_isyieldable));
    coroutine_lib->set("close", std::make_shared<LuaFunctionWrapper>(coroutine_close));

    return coroutine_lib;
}