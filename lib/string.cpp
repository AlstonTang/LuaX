#include "string.hpp"
#include "lua_object.hpp"
#include <string>
#include <algorithm>
#include <vector>
#include <numeric>
#include <regex> // Added for regex functions
#include <iostream> // Added for std::cerr

// Helper function to get a string from a LuaValue
std::string get_string(const LuaValue& v) {
    if (std::holds_alternative<std::string>(v)) {
        return std::get<std::string>(v);
    }
    return "";
}

// string.byte
std::vector<LuaValue> string_byte(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    double i = std::holds_alternative<double>(args->get("2")) ? std::get<double>(args->get("2")) : 1.0;
    double j = std::holds_alternative<double>(args->get("3")) ? std::get<double>(args->get("3")) : i;

    std::vector<LuaValue> results_vec;
    for (int k = static_cast<int>(i) - 1; k < static_cast<int>(j); ++k) {
        if (k >= 0 && k < s.length()) {
            results_vec.push_back(static_cast<double>(static_cast<unsigned char>(s[k])));
        } else {
            break; // Lua returns nothing for out-of-bounds
        }
    }
    return results_vec;
}

// string.char
std::vector<LuaValue> string_char(std::shared_ptr<LuaObject> args) {
    std::string result_str = "";
    for (int i = 1; ; ++i) {
        LuaValue val = args->get(std::to_string(i));
        if (std::holds_alternative<std::monostate>(val)) break;
        result_str += static_cast<char>(static_cast<int>(get_double(val)));
    }
    return {result_str};
}

// string.find
std::vector<LuaValue> string_find(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::string pattern = get_string(args->get("2"));
    double init_double = std::holds_alternative<double>(args->get("3")) ? std::get<double>(args->get("3")) : 1.0;
    bool plain = std::holds_alternative<bool>(args->get("4")) ? std::get<bool>(args->get("4")) : false;

    long long init = static_cast<long long>(init_double);

    if (init < 1) init = 1;
    if (init > s.length()) return {std::monostate{}}; // No match

    std::string search_s = s.substr(init - 1);

    if (plain) {
        size_t pos = search_s.find(pattern);
        if (pos != std::string::npos) {
            return {static_cast<double>(pos + init), static_cast<double>(pos + init + pattern.length() - 1)};
        }
    } else {
        try {
            std::regex re(pattern);
            std::smatch match;
            if (std::regex_search(search_s, match, re)) {
                if (match.size() > 0) {
                    std::vector<LuaValue> results_vec;
                    results_vec.push_back(static_cast<double>(match.position(0) + init));
                    results_vec.push_back(static_cast<double>(match.position(0) + init + match.length(0) - 1));
                    for (size_t i = 1; i < match.size(); ++i) {
                        results_vec.push_back(match.str(i));
                    }
                    return results_vec;
                }
            }
        } catch (const std::regex_error& e) {
            // Handle invalid regex pattern
            std::cerr << "Regex error in string.find: " << e.what() << std::endl;
        }
    }
    return {std::monostate{}}; // No match
}

// string.gsub
std::vector<LuaValue> string_gsub(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::string pattern = get_string(args->get("2"));
    LuaValue repl_val = args->get("3");
    double n_double = std::holds_alternative<double>(args->get("4")) ? std::get<double>(args->get("4")) : -1.0;

    long long n = static_cast<long long>(n_double);
    long long count = 0;
    std::string result = s;

    try {
        std::regex re(pattern);
        if (std::holds_alternative<std::string>(repl_val)) {
            std::string repl_str = std::get<std::string>(repl_val);
            if (n == -1) { // Replace all
                result = std::regex_replace(s, re, repl_str);
                count = std::distance(std::sregex_iterator(s.begin(), s.end(), re), std::sregex_iterator());
            } else { // Replace n times
                std::sregex_iterator it(s.begin(), s.end(), re);
                std::sregex_iterator end;
                std::string current_result = "";
                size_t last_pos = 0;

                for (; it != end && count < n; ++it, ++count) {
                    current_result += it->prefix().str();
                    current_result += repl_str;
                    last_pos = it->position() + it->length();
                }
                current_result += s.substr(last_pos);
                result = current_result;
            }
        } else if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(repl_val)) {
            auto repl_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(repl_val);
            std::sregex_iterator it(s.begin(), s.end(), re);
            std::sregex_iterator end;
            std::string current_result = "";
            size_t last_pos = 0;

            for (; it != end && (n == -1 || count < n); ++it, ++count) {
                current_result += it->prefix().str();
                auto func_args = std::make_shared<LuaObject>();
                for (size_t i = 0; i < it->size(); ++i) {
                    func_args->set(std::to_string(i + 1), it->str(i));
                }
                std::vector<LuaValue> func_return_vec = repl_func->func(func_args);
                if (!func_return_vec.empty()) {
                    current_result += to_cpp_string(func_return_vec[0]);
                }
                last_pos = it->position() + it->length();
            }
            current_result += s.substr(last_pos);
            result = current_result;
        }
    } catch (const std::regex_error& e) {
        std::cerr << "Regex error in string.gsub: " << e.what() << std::endl;
    }

    return {result, static_cast<double>(count)};
}

// string.match
std::vector<LuaValue> string_match(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::string pattern = get_string(args->get("2"));
    double init_double = std::holds_alternative<double>(args->get("3")) ? std::get<double>(args->get("3")) : 1.0;

    long long init = static_cast<long long>(init_double);

    if (init < 1) init = 1;
    if (init > s.length()) return {std::monostate{}}; // No match

    std::string search_s = s.substr(init - 1);

    try {
        std::regex re(pattern);
        std::smatch match;
        if (std::regex_search(search_s, match, re)) {
            if (match.size() > 1) { // Captures
                std::vector<LuaValue> results_vec;
                for (size_t i = 1; i < match.size(); ++i) {
                    results_vec.push_back(match.str(i));
                }
                return results_vec;
            } else if (match.size() == 1) { // Full match, no captures
                return {match.str(0)};
            }
        }
    } catch (const std::regex_error& e) {
        // Handle invalid regex pattern
        std::cerr << "Regex error in string.match: " << e.what() << std::endl;
    }
    return {std::monostate{}}; // No match
}

// string.rep
std::vector<LuaValue> string_rep(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    double n_double = std::holds_alternative<double>(args->get("2")) ? std::get<double>(args->get("2")) : 1.0;
    std::string sep = std::holds_alternative<std::string>(args->get("3")) ? std::get<std::string>(args->get("3")) : "";

    long long n = static_cast<long long>(n_double);

    if (n <= 0) {
        return {""};
    }

    std::string result = "";
    for (long long i = 0; i < n; ++i) {
        result += s;
        if (i < n - 1) {
            result += sep;
        }
    }
    return {result};
}

// string.sub
std::vector<LuaValue> string_sub(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    double i_double = std::holds_alternative<double>(args->get("2")) ? std::get<double>(args->get("2")) : 1.0;
    double j_double = std::holds_alternative<double>(args->get("3")) ? std::get<double>(args->get("3")) : -1.0;

    long long i = static_cast<long long>(i_double);
    long long j = static_cast<long long>(j_double);

    long long len = s.length();

    if (i < 0) {
        i = len + i + 1;
    }
    if (j < 0) {
        j = len + j + 1;
    }

    if (i < 1) i = 1;
    if (j > len) j = len;

    if (i <= j) {
        return {s.substr(i - 1, j - i + 1)};
    }
    return {""};
}

// string.upper
std::vector<LuaValue> string_upper(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return {s};
}

// string.gmatch (not supported in translated environment)
std::vector<LuaValue> string_gmatch(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("string.gmatch is not supported in the translated environment.");
    return {std::monostate{}}; // Should not be reached
}

// string.lower
std::vector<LuaValue> string_lower(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return {s};
}

// string.len
std::vector<LuaValue> string_len(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    return {static_cast<double>(s.length())};
}

// string.reverse
std::vector<LuaValue> string_reverse(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::reverse(s.begin(), s.end());
    return {s};
}

std::shared_ptr<LuaObject> create_string_library() {
    auto string_lib = std::make_shared<LuaObject>();
    string_lib->set("byte", std::make_shared<LuaFunctionWrapper>(string_byte));
    string_lib->set("char", std::make_shared<LuaFunctionWrapper>(string_char));
    string_lib->set("find", std::make_shared<LuaFunctionWrapper>(string_find));
    string_lib->set("gsub", std::make_shared<LuaFunctionWrapper>(string_gsub));
    string_lib->set("match", std::make_shared<LuaFunctionWrapper>(string_match));
    string_lib->set("rep", std::make_shared<LuaFunctionWrapper>(string_rep));
    string_lib->set("sub", std::make_shared<LuaFunctionWrapper>(string_sub));
    string_lib->set("upper", std::make_shared<LuaFunctionWrapper>(string_upper));
    string_lib->set("gmatch", std::make_shared<LuaFunctionWrapper>(string_gmatch));
    string_lib->set("lower", std::make_shared<LuaFunctionWrapper>(string_lower));
    string_lib->set("len", std::make_shared<LuaFunctionWrapper>(string_len));
    string_lib->set("reverse", std::make_shared<LuaFunctionWrapper>(string_reverse));
    return string_lib;
}