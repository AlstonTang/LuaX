#include "math.hpp"
#include "lua_object.hpp"
#include <cmath>
#include <limits>
#include <algorithm>
#include <random>
#include <chrono>
#include <thread>
#include <charconv>
#include <string>

// Constants
constexpr double PI = 3.14159265358979323846;

// Global random number generator
thread_local std::default_random_engine generator;
thread_local bool generator_seeded = false;

static void ensure_seeded() {
	if (!generator_seeded) {
		generator.seed(std::chrono::system_clock::now().time_since_epoch().count() + 
		              std::hash<std::thread::id>{}(std::this_thread::get_id()));
		generator_seeded = true;
	}
}

// Helper function to get a number from a LuaValue
double get_number(const LuaValue& v) {
	if (const double* val = std::get_if<double>(&v)) {
		return *val;
	}

	if (const long long* val = std::get_if<long long>(&v)) {
		return static_cast<double>(*val);
	}

	if (const std::string* pStr = std::get_if<std::string>(&v)) {
		const std::string& s = *pStr;

		if (s.empty()) return 0.0;
		double result;
		const char* start = s.data();
		const char* end = start + s.size();

		auto [ptr, ec] = std::from_chars(start, end, result);

		if (ec == std::errc()) {
			return result;
		}
	}

	return 0.0;
}

// math.randomseed
void math_randomseed(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	ensure_seeded();
	long long seed = static_cast<long long>(get_number(args[0]));
	generator.seed(seed);
	out.assign({std::monostate{}});
	return;
}

// math.random
void math_random(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	ensure_seeded();
	LuaValue arg1 = n_args >= 1 ? args[0] : std::monostate{};
	LuaValue arg2 = n_args >= 2 ? args[1] : std::monostate{};

	if (std::holds_alternative<std::monostate>(arg1)) {
		// math.random() returns a float in [0,1)
		std::uniform_real_distribution<double> distribution(0.0, 1.0);
		out.assign({distribution(generator)});
		return;
	}
	else if (std::holds_alternative<std::monostate>(arg2)) {
		// math.random(m) returns an integer in [1, m]
		int m = static_cast<int>(get_number(arg1));
		std::uniform_int_distribution<int> distribution(1, m);
		out.assign({static_cast<double>(distribution(generator))});
		return;
	}
	else {
		// math.random(m, n) returns an integer in [m, n]
		int m = static_cast<int>(get_number(arg1));
		int n = static_cast<int>(get_number(arg2));
		std::uniform_int_distribution<int> distribution(m, n);
		out.assign({static_cast<double>(distribution(generator))});
		return;
	}
}

// math.abs
void math_abs(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({std::abs(get_number(args[0]))});
	return;
}

// math.acos
void math_acos(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({std::acos(get_number(args[0]))});
	return;
}

// math.asin
void math_asin(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({std::asin(get_number(args[0]))});
	return;
}

// math.atan
void math_atan(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({std::atan(get_number(args[0]))});
	return;
}

// math.ceil
void math_ceil(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({std::ceil(get_number(args[0]))});
	return;
}

// math.cos
void math_cos(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({std::cos(get_number(args[0]))});
	return;
}

// math.deg
void math_deg(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({get_number(args[0]) * 180.0 / PI});
	return;
}

// math.exp
void math_exp(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({std::exp(get_number(args[0]))});
	return;
}

// math.floor
void math_floor(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({std::floor(get_number(args[0]))});
	return;
}

// math.fmod
void math_fmod(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({std::fmod(get_number(args[0]), get_number(args[1]))});
	return;
}

// math.log
void math_log(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({std::log(get_number(args[0]))});
	return;
}

// math.max
void math_max(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args == 0) throw std::runtime_error("bad argument #1 to 'max' (value expected)");
	double max_val = get_number(args[0]);
	for (unsigned long i = 1; i < n_args; ++i) {
		LuaValue val = args[i];
		max_val = std::max(max_val, get_number(val));
	}
	out.assign({max_val});
	return;
}

// math.min
void math_min(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args == 0) throw std::runtime_error("bad argument #1 to 'min' (value expected)");
	double min_val = get_number(args[0]);
	for (unsigned long i = 1; i < n_args; ++i) {
		LuaValue val = args[i];
		min_val = std::min(min_val, get_number(val));
	}
	out.assign({min_val});
	return;
}

// math.modf
void math_modf(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	double intpart;
	double fractpart = std::modf(get_number(args[0]), &intpart);
	out.assign({intpart, fractpart});
	return;
}

// math.rad
void math_rad(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({get_number(args[0]) * PI / 180.0});
	return;
}

// math.sin
void math_sin(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({std::sin(get_number(args[0]))});
	return;
}

// math.pow
void math_pow(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({std::pow(get_number(args[0]), get_number(args[1]))});
	return;
}

// math.sqrt
void math_sqrt(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({std::sqrt(get_number(args[0]))});
	return;
}

// math.tan
void math_tan(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({std::tan(get_number(args[0]))});
	return;
}

// math.tointeger
void math_tointeger(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({static_cast<long long>(get_number(args[0]))});
	return;
}

// math.type
void math_type(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 1) {
		out.assign({LuaValue(std::string_view("nil"))});
		return;
	}
	if (std::holds_alternative<double>(args[0])) {
		out.assign({LuaValue(std::string_view("float"))});
		return;
	}
	else if (std::holds_alternative<long long>(args[0])) {
		out.assign({LuaValue(std::string_view("integer"))});
		return;
	}
	out.assign({LuaValue(std::string_view("nil"))});
	return;
}

// math.ult
void math_ult(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({
		static_cast<unsigned long long>(get_number(args[0])) < static_cast<unsigned long long>(get_number(args[1]))
	});
	return;
}


std::shared_ptr<LuaObject> create_math_library() {
	static std::shared_ptr<LuaObject> math_lib;
	if (math_lib) return math_lib;

	// Seed the random number generator with current time by default
	generator.seed(std::chrono::system_clock::now().time_since_epoch().count());

	math_lib = std::make_shared<LuaObject>();
	math_lib->set("abs", LUA_C_FUNC(math_abs));
	math_lib->set("acos", LUA_C_FUNC(math_acos));
	math_lib->set("asin", LUA_C_FUNC(math_asin));
	math_lib->set("atan", LUA_C_FUNC(math_atan));
	math_lib->set("ceil", LUA_C_FUNC(math_ceil));
	math_lib->set("cos", LUA_C_FUNC(math_cos));
	math_lib->set("deg", LUA_C_FUNC(math_deg));
	math_lib->set("exp", LUA_C_FUNC(math_exp));
	math_lib->set("floor", LUA_C_FUNC(math_floor));
	math_lib->set("fmod", LUA_C_FUNC(math_fmod));
	math_lib->set("log", LUA_C_FUNC(math_log));
	math_lib->set("max", LUA_C_FUNC(math_max));
	math_lib->set("min", LUA_C_FUNC(math_min));
	math_lib->set("modf", LUA_C_FUNC(math_modf));
	math_lib->set("pow", LUA_C_FUNC(math_pow));
	math_lib->set("rad", LUA_C_FUNC(math_rad));
	math_lib->set("random", LUA_C_FUNC(math_random));
	math_lib->set("randomseed", LUA_C_FUNC(math_randomseed));
	math_lib->set("sin", LUA_C_FUNC(math_sin));
	math_lib->set("sqrt", LUA_C_FUNC(math_sqrt));
	math_lib->set("tan", LUA_C_FUNC(math_tan));
	math_lib->set("tointeger", LUA_C_FUNC(math_tointeger));
	math_lib->set("type", LUA_C_FUNC(math_type));
	math_lib->set("ult", LUA_C_FUNC(math_ult));
	math_lib->set("huge", HUGE_VAL);
	math_lib->set("pi", PI);
	math_lib->set("maxinteger", 9223372036854775807LL);
	math_lib->set("mininteger", -9223372036854775807LL - 1);

	return math_lib;
}
