#include "math.hpp"
#include "lua_object.hpp"
#include <cmath>
#include <limits>
#include <algorithm>
#include <random>
#include <chrono>

// Constants
constexpr double PI = 3.14159265358979323846;

// Global random number generator
std::default_random_engine generator;

// Helper function to get a number from a LuaValue
double get_number(const LuaValue& v) {
	if (std::holds_alternative<double>(v)) {
		return std::get<double>(v);
	}
	if (std::holds_alternative<long long>(v)) {
		return static_cast<double>(std::get<long long>(v));
	}
	return 0.0;
}

// math.randomseed
std::vector<LuaValue> math_randomseed(std::shared_ptr<LuaObject> args) {
	long long seed = static_cast<long long>(get_number(args->get("1")));
	generator.seed(seed);
	return {std::monostate{}};
}

// math.random
std::vector<LuaValue> math_random(std::shared_ptr<LuaObject> args) {
	LuaValue arg1 = args->get("1");
	LuaValue arg2 = args->get("2");

	if (std::holds_alternative<std::monostate>(arg1)) {
		// math.random() returns a float in [0,1)
		std::uniform_real_distribution<double> distribution(0.0, 1.0);
		return {distribution(generator)};
	} else if (std::holds_alternative<std::monostate>(arg2)) {
		// math.random(m) returns an integer in [1, m]
		int m = static_cast<int>(get_number(arg1));
		std::uniform_int_distribution<int> distribution(1, m);
		return {static_cast<double>(distribution(generator))};
	} else {
		// math.random(m, n) returns an integer in [m, n]
		int m = static_cast<int>(get_number(arg1));
		int n = static_cast<int>(get_number(arg2));
		std::uniform_int_distribution<int> distribution(m, n);
		return {static_cast<double>(distribution(generator))};
	}
}

// math.abs
std::vector<LuaValue> math_abs(std::shared_ptr<LuaObject> args) {
	return {std::abs(get_number(args->get("1")))};
}

// math.acos
std::vector<LuaValue> math_acos(std::shared_ptr<LuaObject> args) {
	return {std::acos(get_number(args->get("1")))};
}

// math.asin
std::vector<LuaValue> math_asin(std::shared_ptr<LuaObject> args) {
	return {std::asin(get_number(args->get("1")))};
}

// math.atan
std::vector<LuaValue> math_atan(std::shared_ptr<LuaObject> args) {
	return {std::atan(get_number(args->get("1")))};
}

// math.ceil
std::vector<LuaValue> math_ceil(std::shared_ptr<LuaObject> args) {
	return {std::ceil(get_number(args->get("1")))};
}

// math.cos
std::vector<LuaValue> math_cos(std::shared_ptr<LuaObject> args) {
	return {std::cos(get_number(args->get("1")))};
}

// math.deg
std::vector<LuaValue> math_deg(std::shared_ptr<LuaObject> args) {
	return {get_number(args->get("1")) * 180.0 / PI};
}

// math.exp
std::vector<LuaValue> math_exp(std::shared_ptr<LuaObject> args) {
	return {std::exp(get_number(args->get("1")))};
}

// math.floor
std::vector<LuaValue> math_floor(std::shared_ptr<LuaObject> args) {
	return {std::floor(get_number(args->get("1")))};
}

// math.fmod
std::vector<LuaValue> math_fmod(std::shared_ptr<LuaObject> args) {
	return {std::fmod(get_number(args->get("1")), get_number(args->get("2")))};
}

// math.log
std::vector<LuaValue> math_log(std::shared_ptr<LuaObject> args) {
	return {std::log(get_number(args->get("1")))};
}

// math.max
std::vector<LuaValue> math_max(std::shared_ptr<LuaObject> args) {
	double max_val = get_number(args->get("1"));
	for (int i = 2; ; ++i) {
		LuaValue val = args->get(std::to_string(i));
		if (std::holds_alternative<std::monostate>(val)) break;
		max_val = std::max(max_val, get_number(val));
	}
	return {max_val};
}

// math.min
std::vector<LuaValue> math_min(std::shared_ptr<LuaObject> args) {
	double min_val = get_number(args->get("1"));
	for (int i = 2; ; ++i) {
		LuaValue val = args->get(std::to_string(i));
		if (std::holds_alternative<std::monostate>(val)) break;
		min_val = std::min(min_val, get_number(val));
	}
	return {min_val};
}

// math.modf
std::vector<LuaValue> math_modf(std::shared_ptr<LuaObject> args) {
	double intpart;
	double fractpart = std::modf(get_number(args->get("1")), &intpart);
	return {intpart, fractpart};
}

// math.rad
std::vector<LuaValue> math_rad(std::shared_ptr<LuaObject> args) {
	return {get_number(args->get("1")) * PI / 180.0};
}

// math.sin
std::vector<LuaValue> math_sin(std::shared_ptr<LuaObject> args) {
	return {std::sin(get_number(args->get("1")))};
}

// math.sqrt
std::vector<LuaValue> math_sqrt(std::shared_ptr<LuaObject> args) {
	return {std::sqrt(get_number(args->get("1")))};
}

// math.tan
std::vector<LuaValue> math_tan(std::shared_ptr<LuaObject> args) {
	return {std::tan(get_number(args->get("1")))};
}

// math.tointeger
std::vector<LuaValue> math_tointeger(std::shared_ptr<LuaObject> args) {
	return {LuaValue(static_cast<double>(static_cast<long long>(get_number(args->get("1")))))};
}

// math.type
std::vector<LuaValue> math_type(std::shared_ptr<LuaObject> args) {
	if (std::holds_alternative<double>(args->get("1"))) {
		return {"float"};
	}
	return {"integer"};
}

// math.ult
std::vector<LuaValue> math_ult(std::shared_ptr<LuaObject> args) {
	return {static_cast<unsigned long long>(get_number(args->get("1"))) < static_cast<unsigned long long>(get_number(args->get("2")))};
}

std::shared_ptr<LuaObject> create_math_library() {
	auto math_lib = std::make_shared<LuaObject>();

	// Seed the random number generator with current time by default
	generator.seed(std::chrono::system_clock::now().time_since_epoch().count());

	math_lib->properties = {
		{"abs", std::make_shared<LuaFunctionWrapper>(math_abs)},
		{"acos", std::make_shared<LuaFunctionWrapper>(math_acos)},
		{"asin", std::make_shared<LuaFunctionWrapper>(math_asin)},
		{"atan", std::make_shared<LuaFunctionWrapper>(math_atan)},
		{"ceil", std::make_shared<LuaFunctionWrapper>(math_ceil)},
		{"cos", std::make_shared<LuaFunctionWrapper>(math_cos)},
		{"deg", std::make_shared<LuaFunctionWrapper>(math_deg)},
		{"exp", std::make_shared<LuaFunctionWrapper>(math_exp)},
		{"floor", std::make_shared<LuaFunctionWrapper>(math_floor)},
		{"fmod", std::make_shared<LuaFunctionWrapper>(math_fmod)},
		{"log", std::make_shared<LuaFunctionWrapper>(math_log)},
		{"max", std::make_shared<LuaFunctionWrapper>(math_max)},
		{"min", std::make_shared<LuaFunctionWrapper>(math_min)},
		{"modf", std::make_shared<LuaFunctionWrapper>(math_modf)},
		{"rad", std::make_shared<LuaFunctionWrapper>(math_rad)},
		{"random", std::make_shared<LuaFunctionWrapper>(math_random)},
		{"randomseed", std::make_shared<LuaFunctionWrapper>(math_randomseed)},
		{"sin", std::make_shared<LuaFunctionWrapper>(math_sin)},
		{"sqrt", std::make_shared<LuaFunctionWrapper>(math_sqrt)},
		{"tan", std::make_shared<LuaFunctionWrapper>(math_tan)},
		{"tointeger", std::make_shared<LuaFunctionWrapper>(math_tointeger)},
		{"type", std::make_shared<LuaFunctionWrapper>(math_type)},
		{"ult", std::make_shared<LuaFunctionWrapper>(math_ult)},
		{"huge", std::numeric_limits<double>::infinity()},
		{"pi", PI},
		{"maxinteger", static_cast<double>(std::numeric_limits<long long>::max())},
		{"mininteger", static_cast<double>(std::numeric_limits<long long>::min())}
	};

	return math_lib;
}
