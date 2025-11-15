#include "string.hpp"
#include "lua_object.hpp"
#include <cstring>
#include <algorithm>
#include <vector>
#include <numeric>
#include <regex>
#include <iostream>
#include <stdexcept> // For std::runtime_error
#include <cstdio>    // For snprintf
#include <cctype>    // For isdigit, isprint

// --- Helper Functions (assumed to be available or defined here for completeness) ---

// Helper function to get a string from a LuaValue
std::string get_string(const LuaValue& v) {
    if (std::holds_alternative<std::string>(v)) {
        return std::get<std::string>(v);
    }
    return "";
}

// --- String Library Functions ---

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

// string.dump (Not Feasible)
std::vector<LuaValue> string_dump(std::shared_ptr<LuaObject> args) {
    // This function is highly dependent on the Lua VM's internal bytecode representation.
    // In this C++ environment, which re-implements Lua functions on native C++ types,
    // there is no concept of Lua bytecode to dump.
    throw std::runtime_error("string.dump is not supported in this environment.");
    return {};
}

// string.find
std::vector<LuaValue> string_find(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::string pattern = get_string(args->get("2"));
    double init_double = std::holds_alternative<double>(args->get("3")) ? std::get<double>(args->get("3")) : 1.0;
    bool plain = std::holds_alternative<bool>(args->get("4")) ? std::get<bool>(args->get("4")) : false;

    long long init = static_cast<long long>(init_double);

    if (init < 1) init = 1;
    if (init > s.length() + 1) return {std::monostate{}}; // No match
    if (init > s.length()) { // Can match empty pattern at the end
        if (pattern.empty()) {
            return {static_cast<double>(init), static_cast<double>(init - 1)};
        }
        return {std::monostate{}};
    }


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
                        if (match[i].matched) {
                           results_vec.push_back(match.str(i));
                        } else {
                           results_vec.push_back(static_cast<double>(match.position(i) + init));
                        }
                    }
                    return results_vec;
                }
            }
        } catch (const std::regex_error& e) {
            throw std::runtime_error("bad pattern in string.find: " + std::string(e.what()));
        }
    }
    return {std::monostate{}}; // No match
}

// string.format
std::vector<LuaValue> string_format(std::shared_ptr<LuaObject> args) {
    std::string format_str = get_string(args->get("1"));
    std::string result = "";
    int arg_idx = 2;

    for (size_t i = 0; i < format_str.length(); ++i) {
        if (format_str[i] != '%') {
            result += format_str[i];
            continue;
        }

        i++; // Move past '%'
        if (i >= format_str.length()) {
            throw std::runtime_error("invalid format string: unfinished specifier");
        }

        if (format_str[i] == '%') {
            result += '%';
            continue;
        }

        std::string specifier_format = "%";
        size_t spec_start = i;
        while (i < format_str.length() && (strchr("-+ #0", format_str[i]) || isdigit(format_str[i]) || format_str[i] == '.')) {
            i++;
        }
        if (i >= format_str.length()) {
            throw std::runtime_error("invalid format string: unfinished specifier");
        }
        char spec_char = format_str[i];
        specifier_format += format_str.substr(spec_start, i - spec_start + 1);

        LuaValue arg = args->get(std::to_string(arg_idx++));
        char buffer[2048]; // A larger buffer for safety

        switch (spec_char) {
            case 'c': {
                snprintf(buffer, sizeof(buffer), specifier_format.c_str(), static_cast<char>(get_double(arg)));
                break;
            }
            case 'd': case 'i': {
                snprintf(buffer, sizeof(buffer), specifier_format.c_str(), static_cast<long long>(get_double(arg)));
                break;
            }
            case 'o': case 'u': case 'x': case 'X': {
                 snprintf(buffer, sizeof(buffer), specifier_format.c_str(), static_cast<unsigned long long>(get_double(arg)));
                 break;
            }
            case 'e': case 'E': case 'f': case 'g': case 'G': case 'a': case 'A': {
                snprintf(buffer, sizeof(buffer), specifier_format.c_str(), get_double(arg));
                break;
            }
            case 's': {
                std::string str_arg = to_cpp_string(arg);
                // Note: This is a simplified handling. A full implementation would need to handle precision for strings.
                snprintf(buffer, sizeof(buffer), specifier_format.c_str(), str_arg.c_str());
                break;
            }
            case 'q': {
                std::string s_arg = to_cpp_string(arg);
                std::string quoted = "\"";
                for (char c : s_arg) {
                    switch (c) {
                        case '"':  quoted += "\\\""; break;
                        case '\\': quoted += "\\\\"; break;
                        case '\n': quoted += "\\n"; break;
                        case '\r': quoted += "\\r"; break;
                        default:
                            if (isprint(static_cast<unsigned char>(c))) {
                                quoted += c;
                            } else {
                                char escape_buf[5];
                                sprintf(escape_buf, "\\%03d", static_cast<unsigned char>(c));
                                quoted += escape_buf;
                            }
                    }
                }
                quoted += "\"";
                result += quoted;
                buffer[0] = '\0'; // Mark as handled
                break;
            }
            default:
                throw std::runtime_error("invalid option '" + std::string(1, spec_char) + "' to 'format'");
        }
        result += buffer;
    }
    return {result};
}

// string.gmatch
std::vector<LuaValue> string_gmatch(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::string pattern = get_string(args->get("2"));

    // The regex object must live as long as the iterators.
    // We capture it in the lambda by value (via make_shared) to ensure its lifetime.
    auto re = std::make_shared<std::regex>();
    try {
        *re = std::regex(pattern);
    } catch (const std::regex_error& e) {
        throw std::runtime_error("bad pattern in string.gmatch: " + std::string(e.what()));
    }
    
    // The state of the iteration is stored in the iterator pointers.
    auto it_ptr = std::make_shared<std::sregex_iterator>(s.begin(), s.end(), *re);
    auto end_ptr = std::make_shared<std::sregex_iterator>();

    // The iterator function to be returned. It captures the state.
    auto iterator_func = [re, it_ptr, end_ptr](std::shared_ptr<LuaObject> /* ignored */) -> std::vector<LuaValue> {
        if (*it_ptr == *end_ptr) {
            return {}; // Corresponds to Lua's nil, ending the loop
        }

        std::smatch match = **it_ptr;
        (*it_ptr)++;

        std::vector<LuaValue> results_vec;
        if (match.size() > 1) { // Has captures
            for (size_t i = 1; i < match.size(); ++i) {
                results_vec.push_back(match.str(i));
            }
        } else { // No captures, return the whole match
            results_vec.push_back(match.str(0));
        }
        return results_vec;
    };

    return {std::make_shared<LuaFunctionWrapper>(iterator_func)};
}

// string.gsub
std::vector<LuaValue> string_gsub(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::string pattern = get_string(args->get("2"));
    LuaValue repl_val = args->get("3");
    double n_double = std::holds_alternative<double>(args->get("4")) ? std::get<double>(args->get("4")) : s.length() + 1;

    long long n = static_cast<long long>(n_double);
    long long count = 0;
    std::string result;
    std::string::const_iterator search_start = s.cbegin();

    try {
        std::regex re(pattern);
        std::smatch match;

        while ((n < 0 || count < n) && std::regex_search(search_start, s.cend(), match, re)) {
            result.append(match.prefix());
            count++;

            if (std::holds_alternative<std::string>(repl_val)) {
                std::string repl_str = std::get<std::string>(repl_val);
                result.append(match.format(repl_str, std::regex_constants::format_sed));
            } else if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(repl_val)) {
                auto repl_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(repl_val);
                auto func_args = std::make_shared<LuaObject>();
                if (match.size() > 1) { // Captures
                    for (size_t i = 1; i < match.size(); ++i) {
                        func_args->set(std::to_string(i), match.str(i));
                    }
                } else { // No captures, pass full match
                    func_args->set("1", match.str(0));
                }
                std::vector<LuaValue> func_return_vec = repl_func->func(func_args);
                if (!func_return_vec.empty()) {
                    result += to_cpp_string(func_return_vec[0]);
                }
            } else if (std::holds_alternative<std::shared_ptr<LuaObject>>(repl_val)) {
                // Table replacement
                auto tbl = std::get<std::shared_ptr<LuaObject>>(repl_val);
                std::string key = (match.size() > 1) ? match.str(1) : match.str(0);
                LuaValue tbl_val = tbl->get(key);
                if (!std::holds_alternative<std::monostate>(tbl_val)) {
                    result += to_cpp_string(tbl_val);
                } else {
                    result += match.str(0);
                }
            }

            search_start = match.suffix().first;
            if (match.length() == 0) { // Handle empty matches
                if (search_start != s.cend()) {
                    result += *search_start;
                    search_start++;
                } else {
                    break;
                }
            }
        }
        result.append(search_start, s.cend());

    } catch (const std::regex_error& e) {
        throw std::runtime_error("bad pattern in string.gsub: " + std::string(e.what()));
    }

    return {result, static_cast<double>(count)};
}

// string.len
std::vector<LuaValue> string_len(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    return {static_cast<double>(s.length())};
}

// string.lower
std::vector<LuaValue> string_lower(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return {s};
}

// string.match
std::vector<LuaValue> string_match(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::string pattern = get_string(args->get("2"));
    double init_double = std::holds_alternative<double>(args->get("3")) ? std::get<double>(args->get("3")) : 1.0;

    long long init = static_cast<long long>(init_double);

    if (init < 1) init = 1;
    if (init > s.length() + 1) return {std::monostate{}};
    
    std::string::const_iterator search_start = s.cbegin() + (init - 1);

    try {
        std::regex re(pattern);
        std::smatch match;
        if (std::regex_search(search_start, s.cend(), match, re)) {
            if (match.size() > 1) { // Captures
                std::vector<LuaValue> results_vec;
                for (size_t i = 1; i < match.size(); ++i) {
                    results_vec.push_back(match.str(i));
                }
                return results_vec;
            } else { // Full match, no captures
                return {match.str(0)};
            }
        }
    } catch (const std::regex_error& e) {
        throw std::runtime_error("bad pattern in string.match: " + std::string(e.what()));
    }
    return {std::monostate{}}; // No match
}

// string.pack (Not Feasible)
std::vector<LuaValue> string_pack(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("string.pack is not supported due to its complexity.");
    return {};
}

// string.packsize (Not Feasible)
std::vector<LuaValue> string_packsize(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("string.packsize is not supported due to its complexity.");
    return {};
}

// string.rep
std::vector<LuaValue> string_rep(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    double n_double = std::holds_alternative<double>(args->get("2")) ? get_double(args->get("2")) : 1.0;
    std::string sep = std::holds_alternative<std::string>(args->get("3")) ? std::get<std::string>(args->get("3")) : "";

    long long n = static_cast<long long>(n_double);

    if (n <= 0) {
        return {""};
    }

    std::string result = "";
    for (long long i = 0; i < n; ++i) {
        result += s;
        if (!sep.empty() && i < n - 1) {
            result += sep;
        }
    }
    return {result};
}

// string.reverse
std::vector<LuaValue> string_reverse(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::reverse(s.begin(), s.end());
    return {s};
}

// string.sub
std::vector<LuaValue> string_sub(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    double i_double = std::holds_alternative<double>(args->get("2")) ? get_double(args->get("2")) : 1.0;
    double j_double = std::holds_alternative<double>(args->get("3")) ? get_double(args->get("3")) : -1.0;

    long long i = static_cast<long long>(i_double);
    long long j = static_cast<long long>(j_double);
    long long len = s.length();

    if (i < 0) i = len + i + 1;
    if (j < 0) j = len + j + 1;
    if (i < 1) i = 1;
    if (j > len) j = len;

    if (i <= j) {
        return {s.substr(i - 1, j - i + 1)};
    }
    return {""};
}

// string.unpack (Not Feasible)
std::vector<LuaValue> string_unpack(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("string.unpack is not supported due to its complexity.");
    return {};
}

// string.upper
std::vector<LuaValue> string_upper(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return {s};
}


// --- Library Creation ---

std::shared_ptr<LuaObject> create_string_library() {
    auto string_lib = std::make_shared<LuaObject>();
    string_lib->set("byte", std::make_shared<LuaFunctionWrapper>(string_byte));
    string_lib->set("char", std::make_shared<LuaFunctionWrapper>(string_char));
    string_lib->set("dump", std::make_shared<LuaFunctionWrapper>(string_dump));
    string_lib->set("find", std::make_shared<LuaFunctionWrapper>(string_find));
    string_lib->set("format", std::make_shared<LuaFunctionWrapper>(string_format));
    string_lib->set("gmatch", std::make_shared<LuaFunctionWrapper>(string_gmatch));
    string_lib->set("gsub", std::make_shared<LuaFunctionWrapper>(string_gsub));
    string_lib->set("len", std::make_shared<LuaFunctionWrapper>(string_len));
    string_lib->set("lower", std::make_shared<LuaFunctionWrapper>(string_lower));
    string_lib->set("match", std::make_shared<LuaFunctionWrapper>(string_match));
    string_lib->set("pack", std::make_shared<LuaFunctionWrapper>(string_pack));
    string_lib->set("packsize", std::make_shared<LuaFunctionWrapper>(string_packsize));
    string_lib->set("rep", std::make_shared<LuaFunctionWrapper>(string_rep));
    string_lib->set("reverse", std::make_shared<LuaFunctionWrapper>(string_reverse));
    string_lib->set("sub", std::make_shared<LuaFunctionWrapper>(string_sub));
    string_lib->set("unpack", std::make_shared<LuaFunctionWrapper>(string_unpack));
    string_lib->set("upper", std::make_shared<LuaFunctionWrapper>(string_upper));
    return string_lib;
}