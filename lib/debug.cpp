#include "debug.hpp"
#include "lua_object.hpp"
#include <stdexcept>

void debug_debug(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.debug is not supported in the translated environment.");
}

void debug_gethook(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.gethook is not supported in the translated environment.");
}

void debug_getinfo(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.getinfo is not supported in the translated environment.");
}

void debug_getlocal(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.getlocal is not supported in the translated environment.");
}

void debug_getmetatable(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.getmetatable is not supported in the translated environment.");
}

void debug_getregistry(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.getregistry is not supported in the translated environment.");
}

void debug_getupvalue(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.getupvalue is not supported in the translated environment.");
}

void debug_getuservalue(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.getuservalue is not supported in the translated environment.");
}

void debug_sethook(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.sethook is not supported in the translated environment.");
}

void debug_setlocal(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.setlocal is not supported in the translated environment.");
}

void debug_setmetatable(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.setmetatable is not supported in the translated environment.");
}

void debug_setupvalue(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.setupvalue is not supported in the translated environment.");
}

void debug_setuservalue(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.setuservalue is not supported in the translated environment.");
}

void debug_traceback(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.traceback is not supported in the translated environment.");
}

void debug_upvalueid(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.upvalueid is not supported in the translated environment.");
}

void debug_upvaluejoin(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("debug.upvaluejoin is not supported in the translated environment.");
}

std::shared_ptr<LuaObject> create_debug_library() {
	static std::shared_ptr<LuaObject> debug_lib;
	if (debug_lib) return debug_lib;

	debug_lib = std::make_shared<LuaObject>();
	debug_lib->set("debug", LUA_C_FUNC(debug_debug));
	debug_lib->set("getuservalue", LUA_C_FUNC(debug_getuservalue));
	debug_lib->set("gethook", LUA_C_FUNC(debug_gethook));
	debug_lib->set("getinfo", LUA_C_FUNC(debug_getinfo));
	debug_lib->set("getlocal", LUA_C_FUNC(debug_getlocal));
	debug_lib->set("getmetatable", LUA_C_FUNC(debug_getmetatable));
	debug_lib->set("getregistry", LUA_C_FUNC(debug_getregistry));
	debug_lib->set("getupvalue", LUA_C_FUNC(debug_getupvalue));
	debug_lib->set("sethook", LUA_C_FUNC(debug_sethook));
	debug_lib->set("setlocal", LUA_C_FUNC(debug_setlocal));
	debug_lib->set("setmetatable", LUA_C_FUNC(debug_setmetatable));
	debug_lib->set("setupvalue", LUA_C_FUNC(debug_setupvalue));
	debug_lib->set("setuservalue", LUA_C_FUNC(debug_setuservalue));
	debug_lib->set("traceback", LUA_C_FUNC(debug_traceback));
	debug_lib->set("upvalueid", LUA_C_FUNC(debug_upvalueid));
	debug_lib->set("upvaluejoin", LUA_C_FUNC(debug_upvaluejoin));

	return debug_lib;
}
