#ifndef STRING_HPP
#define STRING_HPP

#include "lua_object.hpp"
#include "lua_value.hpp" // Include LuaValue
#include <memory>
#include <regex> // For std::smatch, std::regex_search, std::regex_replace

std::shared_ptr<LuaObject> create_string_library();

// --- String Method Helpers ---
// These functions wrap the C++ logic and return a std::vector<LuaValue>
// to match the return type of a translated function call.

inline std::vector<LuaValue> lua_string_match(const LuaValue& str, const LuaValue& pattern) {
    std::string s = to_cpp_string(str);
    std::string p = to_cpp_string(pattern);
    std::smatch match;
    if (std::regex_search(s, match, std::regex(p))) {
        if (match.size() > 1) {
            return {std::string(match[1])}; // Return first capture
        }
        return {std::string(match[0])}; // Return full match
    }
    return {std::monostate{}}; // nil
}

inline std::vector<LuaValue> lua_string_find(const LuaValue& str, const LuaValue& pattern) {
    size_t pos = to_cpp_string(str).find(to_cpp_string(pattern));
    if (pos != std::string::npos) {
        return {static_cast<long long>(pos + 1)}; // Lua is 1-indexed
    }
    return {std::monostate{}}; // nil
}

inline std::vector<LuaValue> lua_string_gsub(const LuaValue& str, const LuaValue& pattern, const LuaValue& replacement) {
    std::string s = to_cpp_string(str);
    std::string p = to_cpp_string(pattern);
    std::string r = to_cpp_string(replacement);
    return {std::regex_replace(s, std::regex(p), r)};
}

#endif // STRING_HPP
