#include "math.hpp"
#include "lua_object.hpp"
#include <cmath>
#include <limits>
#include <algorithm>

// Helper function to get a number from a LuaValue
double get_number(const LuaValue& v) {
    if (std::holds_alternative<double>(v)) {
        return std::get<double>(v);
    }
    return 0.0;
}

// math.abs
LuaValue math_abs(std::shared_ptr<LuaObject> args) {
    return std::abs(get_number(args->get("1")));
}

// math.acos
LuaValue math_acos(std::shared_ptr<LuaObject> args) {
    return std::acos(get_number(args->get("1")));
}

// math.asin
LuaValue math_asin(std::shared_ptr<LuaObject> args) {
    return std::asin(get_number(args->get("1")));
}

// math.atan
LuaValue math_atan(std::shared_ptr<LuaObject> args) {
    return std::atan(get_number(args->get("1")));
}

// math.ceil
LuaValue math_ceil(std::shared_ptr<LuaObject> args) {
    return std::ceil(get_number(args->get("1")));
}

// math.cos
LuaValue math_cos(std::shared_ptr<LuaObject> args) {
    return std::cos(get_number(args->get("1")));
}

// math.deg
LuaValue math_deg(std::shared_ptr<LuaObject> args) {
    return get_number(args->get("1")) * 180.0 / 3.14159265358979323846;
}

// math.exp
LuaValue math_exp(std::shared_ptr<LuaObject> args) {
    return std::exp(get_number(args->get("1")));
}

// math.floor
LuaValue math_floor(std::shared_ptr<LuaObject> args) {
    return std::floor(get_number(args->get("1")));
}

// math.fmod
LuaValue math_fmod(std::shared_ptr<LuaObject> args) {
    return std::fmod(get_number(args->get("1")), get_number(args->get("2")));
}

// math.log
LuaValue math_log(std::shared_ptr<LuaObject> args) {
    return std::log(get_number(args->get("1")));
}

// math.max
LuaValue math_max(std::shared_ptr<LuaObject> args) {
    double max_val = get_number(args->get("1"));
    for (int i = 2; ; ++i) {
        LuaValue val = args->get(std::to_string(i));
        if (std::holds_alternative<std::monostate>(val)) break;
        max_val = std::max(max_val, get_number(val));
    }
    return max_val;
}

// math.min
LuaValue math_min(std::shared_ptr<LuaObject> args) {
    double min_val = get_number(args->get("1"));
    for (int i = 2; ; ++i) {
        LuaValue val = args->get(std::to_string(i));
        if (std::holds_alternative<std::monostate>(val)) break;
        min_val = std::min(min_val, get_number(val));
    }
    return min_val;
}

// math.modf
LuaValue math_modf(std::shared_ptr<LuaObject> args) {
    double intpart;
    double fractpart = std::modf(get_number(args->get("1")), &intpart);
    auto results = std::make_shared<LuaObject>();
    results->set("1", intpart);
    results->set("2", fractpart);
    return results;
}

// math.rad
LuaValue math_rad(std::shared_ptr<LuaObject> args) {
    return get_number(args->get("1")) * 3.14159265358979323846 / 180.0;
}

// math.sin
LuaValue math_sin(std::shared_ptr<LuaObject> args) {
    return std::sin(get_number(args->get("1")));
}

// math.sqrt
LuaValue math_sqrt(std::shared_ptr<LuaObject> args) {
    return std::sqrt(get_number(args->get("1")));
}

// math.tan
LuaValue math_tan(std::shared_ptr<LuaObject> args) {
    return std::tan(get_number(args->get("1")));
}

// math.tointeger
LuaValue math_tointeger(std::shared_ptr<LuaObject> args) {
    return LuaValue(static_cast<double>(static_cast<long long>(get_number(args->get("1")))));
}

// math.type
LuaValue math_type(std::shared_ptr<LuaObject> args) {
    if (std::holds_alternative<double>(args->get("1"))) {
        return "float";
    }
    return "integer";
}

// math.ult
LuaValue math_ult(std::shared_ptr<LuaObject> args) {
    return static_cast<unsigned long long>(get_number(args->get("1"))) < static_cast<unsigned long long>(get_number(args->get("2")));
}

std::shared_ptr<LuaObject> create_math_library() {
    auto math_lib = std::make_shared<LuaObject>();

    math_lib->set("abs", std::make_shared<LuaFunctionWrapper>(math_abs));
    math_lib->set("acos", std::make_shared<LuaFunctionWrapper>(math_acos));
    math_lib->set("asin", std::make_shared<LuaFunctionWrapper>(math_asin));
    math_lib->set("atan", std::make_shared<LuaFunctionWrapper>(math_atan));
    math_lib->set("ceil", std::make_shared<LuaFunctionWrapper>(math_ceil));
    math_lib->set("cos", std::make_shared<LuaFunctionWrapper>(math_cos));
    math_lib->set("deg", std::make_shared<LuaFunctionWrapper>(math_deg));
    math_lib->set("exp", std::make_shared<LuaFunctionWrapper>(math_exp));
    math_lib->set("floor", std::make_shared<LuaFunctionWrapper>(math_floor));
    math_lib->set("fmod", std::make_shared<LuaFunctionWrapper>(math_fmod));
    math_lib->set("log", std::make_shared<LuaFunctionWrapper>(math_log));
    math_lib->set("max", std::make_shared<LuaFunctionWrapper>(math_max));
    math_lib->set("min", std::make_shared<LuaFunctionWrapper>(math_min));
    math_lib->set("modf", std::make_shared<LuaFunctionWrapper>(math_modf));
    math_lib->set("rad", std::make_shared<LuaFunctionWrapper>(math_rad));
    math_lib->set("sin", std::make_shared<LuaFunctionWrapper>(math_sin));
    math_lib->set("sqrt", std::make_shared<LuaFunctionWrapper>(math_sqrt));
    math_lib->set("tan", std::make_shared<LuaFunctionWrapper>(math_tan));
    math_lib->set("tointeger", std::make_shared<LuaFunctionWrapper>(math_tointeger));
    math_lib->set("type", std::make_shared<LuaFunctionWrapper>(math_type));
    math_lib->set("ult", std::make_shared<LuaFunctionWrapper>(math_ult));

    math_lib->set("huge", std::numeric_limits<double>::infinity());
    math_lib->set("pi", 3.14159265358979323846);
    math_lib->set("maxinteger", static_cast<double>(std::numeric_limits<long long>::max()));
    math_lib->set("mininteger", static_cast<double>(std::numeric_limits<long long>::min()));

    return math_lib;
}
