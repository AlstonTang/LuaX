#include "debug.hpp"
#include "lua_object.hpp"
#include <stdexcept>

std::vector<LuaValue> debug_debug(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.debug is not supported in the translated environment.");
}

std::vector<LuaValue> debug_gethook(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.gethook is not supported in the translated environment.");
}

std::vector<LuaValue> debug_getinfo(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.getinfo is not supported in the translated environment.");
}

std::vector<LuaValue> debug_getlocal(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.getlocal is not supported in the translated environment.");
}

std::vector<LuaValue> debug_getmetatable(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.getmetatable is not supported in the translated environment.");
}

std::vector<LuaValue> debug_getregistry(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.getregistry is not supported in the translated environment.");
}

std::vector<LuaValue> debug_getupvalue(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.getupvalue is not supported in the translated environment.");
}

std::vector<LuaValue> debug_getuservalue(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.getuservalue is not supported in the translated environment.");
}

std::vector<LuaValue> debug_sethook(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.sethook is not supported in the translated environment.");
}

std::vector<LuaValue> debug_setlocal(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.setlocal is not supported in the translated environment.");
}

std::vector<LuaValue> debug_setmetatable(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.setmetatable is not supported in the translated environment.");
}

std::vector<LuaValue> debug_setupvalue(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.setupvalue is not supported in the translated environment.");
}

std::vector<LuaValue> debug_setuservalue(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.setuservalue is not supported in the translated environment.");
}

std::vector<LuaValue> debug_traceback(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.traceback is not supported in the translated environment.");
}

std::vector<LuaValue> debug_upvalueid(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.upvalueid is not supported in the translated environment.");
}

std::vector<LuaValue> debug_upvaluejoin(std::vector<LuaValue> args) {
	throw std::runtime_error("debug.upvaluejoin is not supported in the translated environment.");
}

std::shared_ptr<LuaObject> create_debug_library() {
	static std::shared_ptr<LuaObject> debug_lib;
	if (debug_lib) return debug_lib;

	debug_lib = std::make_shared<LuaObject>();

	debug_lib->properties = {
		{"debug", std::make_shared<LuaFunctionWrapper>(debug_debug)},
		{"gethook", std::make_shared<LuaFunctionWrapper>(debug_gethook)},
		{"getinfo", std::make_shared<LuaFunctionWrapper>(debug_getinfo)},
		{"getlocal", std::make_shared<LuaFunctionWrapper>(debug_getlocal)},
		{"getmetatable", std::make_shared<LuaFunctionWrapper>(debug_getmetatable)},
		{"getregistry", std::make_shared<LuaFunctionWrapper>(debug_getregistry)},
		{"getupvalue", std::make_shared<LuaFunctionWrapper>(debug_getupvalue)},
		{"getuservalue", std::make_shared<LuaFunctionWrapper>(debug_getuservalue)},
		{"sethook", std::make_shared<LuaFunctionWrapper>(debug_sethook)},
		{"setlocal", std::make_shared<LuaFunctionWrapper>(debug_setlocal)},
		{"setmetatable", std::make_shared<LuaFunctionWrapper>(debug_setmetatable)},
		{"setupvalue", std::make_shared<LuaFunctionWrapper>(debug_setupvalue)},
		{"setuservalue", std::make_shared<LuaFunctionWrapper>(debug_setuservalue)},
		{"traceback", std::make_shared<LuaFunctionWrapper>(debug_traceback)},
		{"upvalueid", std::make_shared<LuaFunctionWrapper>(debug_upvalueid)},
		{"upvaluejoin", std::make_shared<LuaFunctionWrapper>(debug_upvaluejoin)}
	};

	return debug_lib;
}