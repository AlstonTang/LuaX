#include "debug.hpp"
#include "lua_object.hpp"
#include <stdexcept>

void debug_debug(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("debug.debug is not supported in the translated environment.");
}

void debug_gethook(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("debug.gethook is not supported in the translated environment.");
}

void debug_getinfo(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("debug.getinfo is not supported in the translated environment.");
}

void debug_getlocal(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("debug.getlocal is not supported in the translated environment.");
}

void debug_getmetatable(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("debug.getmetatable is not supported in the translated environment.");
}

void debug_getregistry(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("debug.getregistry is not supported in the translated environment.");
}

void debug_getupvalue(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("debug.getupvalue is not supported in the translated environment.");
}

void debug_getuservalue(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("debug.getuservalue is not supported in the translated environment.");
}

void debug_sethook(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("debug.sethook is not supported in the translated environment.");
}

void debug_setlocal(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("debug.setlocal is not supported in the translated environment.");
}

void debug_setmetatable(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("debug.setmetatable is not supported in the translated environment.");
}

void debug_setupvalue(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("debug.setupvalue is not supported in the translated environment.");
}

void debug_setuservalue(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("debug.setuservalue is not supported in the translated environment.");
}

void debug_traceback(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("debug.traceback is not supported in the translated environment.");
}

void debug_upvalueid(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("debug.upvalueid is not supported in the translated environment.");
}

void debug_upvaluejoin(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
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
