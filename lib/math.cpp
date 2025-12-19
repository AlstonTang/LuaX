#include "math.hpp"
#include "lua_object.hpp"
#include <cmath>
#include <limits>
#include <algorithm>
#include <random>
#include <chrono>
#include <charconv>
#include <string>

// Constants
constexpr double PI = 3.14159265358979323846;

// Global random number generator
std::default_random_engine generator;

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
void math_randomseed(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	long long seed = static_cast<long long>(get_number(args[0]));
	generator.seed(seed);
	out.assign({std::monostate{}});
	return;
}

// math.random
void math_random(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
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
void math_abs(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({std::abs(get_number(args[0]))});
	return;
}

// math.acos
void math_acos(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({std::acos(get_number(args[0]))});
	return;
}

// math.asin
void math_asin(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({std::asin(get_number(args[0]))});
	return;
}

// math.atan
void math_atan(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({std::atan(get_number(args[0]))});
	return;
}

// math.ceil
void math_ceil(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({std::ceil(get_number(args[0]))});
	return;
}

// math.cos
void math_cos(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({std::cos(get_number(args[0]))});
	return;
}

// math.deg
void math_deg(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({get_number(args[0]) * 180.0 / PI});
	return;
}

// math.exp
void math_exp(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({std::exp(get_number(args[0]))});
	return;
}

// math.floor
void math_floor(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({std::floor(get_number(args[0]))});
	return;
}

// math.fmod
void math_fmod(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({std::fmod(get_number(args[0]), get_number(args[1]))});
	return;
}

// math.log
void math_log(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({std::log(get_number(args[0]))});
	return;
}

// math.max
void math_max(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
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
void math_min(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
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
void math_modf(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	double intpart;
	double fractpart = std::modf(get_number(args[0]), &intpart);
	out.assign({intpart, fractpart});
	return;
}

// math.rad
void math_rad(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({get_number(args[0]) * PI / 180.0});
	return;
}

// math.sin
void math_sin(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({std::sin(get_number(args[0]))});
	return;
}

// math.sqrt
void math_sqrt(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({std::sqrt(get_number(args[0]))});
	return;
}

// math.tan
void math_tan(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({std::tan(get_number(args[0]))});
	return;
}

// math.tointeger
void math_tointeger(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({static_cast<long long>(get_number(args[0]))});
	return;
}

// math.type
void math_type(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args < 1) {
		out.assign({"nil"});
		return;
	}
	if (std::holds_alternative<double>(args[0])) {
		out.assign({"float"});
		return;
	}
	else if (std::holds_alternative<long long>(args[0])) {
		out.assign({"integer"});
		return;
	}
	out.assign({"nil"});
	return;
}

// math.ult
void math_ult(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({
		static_cast<unsigned long long>(get_number(args[0])) < static_cast<unsigned long long>(get_number(args[1]))
	});
	return;
}


std::shared_ptr<LuaObject> create_math_library() {
	static std::shared_ptr<LuaObject> math_lib;
	if (math_lib) return math_lib;

	math_lib = std::make_shared<LuaObject>();

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
