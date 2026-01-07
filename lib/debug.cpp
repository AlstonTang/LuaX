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

	debug_lib = LuaObject::create({
		{LuaValue(std::string_view("debug")), std::make_shared<LuaFunctionWrapper>(debug_debug)},
		{LuaValue(std::string_view("gethook")), std::make_shared<LuaFunctionWrapper>(debug_gethook)},
		{LuaValue(std::string_view("getinfo")), std::make_shared<LuaFunctionWrapper>(debug_getinfo)},
		{LuaValue(std::string_view("getlocal")), std::make_shared<LuaFunctionWrapper>(debug_getlocal)},
		{LuaValue(std::string_view("getmetatable")), std::make_shared<LuaFunctionWrapper>(debug_getmetatable)},
		{LuaValue(std::string_view("getregistry")), std::make_shared<LuaFunctionWrapper>(debug_getregistry)},
		{LuaValue(std::string_view("getupvalue")), std::make_shared<LuaFunctionWrapper>(debug_getupvalue)},
		{LuaValue(std::string_view("getuservalue")), std::make_shared<LuaFunctionWrapper>(debug_getuservalue)},
		{LuaValue(std::string_view("sethook")), std::make_shared<LuaFunctionWrapper>(debug_sethook)},
		{LuaValue(std::string_view("setlocal")), std::make_shared<LuaFunctionWrapper>(debug_setlocal)},
		{LuaValue(std::string_view("setmetatable")), std::make_shared<LuaFunctionWrapper>(debug_setmetatable)},
		{LuaValue(std::string_view("setupvalue")), std::make_shared<LuaFunctionWrapper>(debug_setupvalue)},
		{LuaValue(std::string_view("setuservalue")), std::make_shared<LuaFunctionWrapper>(debug_setuservalue)},
		{LuaValue(std::string_view("traceback")), std::make_shared<LuaFunctionWrapper>(debug_traceback)},
		{LuaValue(std::string_view("upvalueid")), std::make_shared<LuaFunctionWrapper>(debug_upvalueid)},
		{LuaValue(std::string_view("upvaluejoin")), std::make_shared<LuaFunctionWrapper>(debug_upvaluejoin)}
	});

	return debug_lib;
}
