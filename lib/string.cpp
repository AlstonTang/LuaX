#include "string.hpp"
#include "lua_object.hpp"
#include <string>
#include <algorithm>
#include <vector>
#include <numeric>

// Helper function to get a string from a LuaValue
std::string get_string(const LuaValue& v) {
    if (std::holds_alternative<std::string>(v)) {
        return std::get<std::string>(v);
    }
    return "";
}

// string.len
LuaValue string_len(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    return static_cast<double>(s.length());
}

// string.reverse
LuaValue string_reverse(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::reverse(s.begin(), s.end());
    return s;
}

std::shared_ptr<LuaObject> create_string_library() {
    auto string_lib = std::make_shared<LuaObject>();

    string_lib->set("len", std::make_shared<LuaFunctionWrapper>(string_len));
    string_lib->set("reverse", std::make_shared<LuaFunctionWrapper>(string_reverse));

    return string_lib;
}
