#ifndef MATH_WRAPPER_HPP
#define MATH_WRAPPER_HPP

#include <cmath>
#include <climits>
#include <lua_object.hpp>
#include <string>
#include <variant>
#include <optional>

/**
 * @brief Custom namespace to provide an alternative, clear syntax for
 * standard lua math functions (e.g., math::sin() instead of std::sin()).
 */
namespace math {
    using std::abs;
    using std::acos;
    using std::asin;
    using std::atan;
    using std::ceil;
    using std::cos;
    using std::exp;
    using std::floor;
    using std::fmod;
    using std::log;
    using std::max;
    using std::min;
    using std::modf;
    using std::sin;
    using std::sqrt;
    using std::tan;

    constexpr double pi = 3.141592653589793;
    constexpr int maxinteger = std::numeric_limits<int>::max();
    constexpr int mininteger = std::numeric_limits<int>::min();

    inline std::optional<int> tointeger(double x) {
        double integralPart;
        if (std::modf(x, &integralPart) != 0.0) {
            return std::nullopt;
        }
        if (x > maxinteger || x < mininteger) {
            return std::nullopt;
        }
        return int(x);
    }

    inline double ult(double a, double b) {
        return std::abs(a) < std::abs(b);
    }

    inline double rad(double deg) {
        return deg * (pi/180);
    }

    inline double deg(double rad) {
        return rad * (180/pi);
    }

    inline std::string type(LuaValue& obj) {
        if (not std::holds_alternative<double>(obj)) {
            return nullptr;
        }
        
        double doubleVal = get_double(obj);
        double integralPart;
        if (std::modf(doubleVal, &integralPart) != 0.0) {
            return "float";
        }

        if (doubleVal > maxinteger ||
            doubleVal < mininteger) {
            return "float";
        }

        return "integer";
    }

    double random();
    int random(int upper);
    int random(int lower, int upper);

    void randomseed(int seed);
    constexpr double huge = INFINITY;
}

#endif // MATH_WRAPPER_HPP