#include "string.hpp"
#include "lua_object.hpp"
#include <cstring>
#include <algorithm>
#include <vector>
#include <numeric>
#include <regex>

#include <stdexcept>
#include <cstdio>
#include <cctype>
#include <sstream>
#include <map>

// --- Helper Functions ---

inline std::string get_string(const LuaValue& v) {
    if (std::holds_alternative<std::string>(v)) {
        return std::get<std::string>(v);
    } else if (std::holds_alternative<double>(v)) {
        double d = std::get<double>(v);
        // Remove trailing zeros for integers if needed, simplified here
        char buf[64];
        snprintf(buf, 64, "%.14g", d);
        return std::string(buf);
    }
    return "";
}

std::string lua_pattern_to_regex(const std::string& lua_pat) {
    std::stringstream ss;
    bool in_bracket = false;

    for (size_t i = 0; i < lua_pat.length(); ++i) {
        char c = lua_pat[i];

        if (c == '%') {
            if (i + 1 >= lua_pat.length()) {
                throw std::runtime_error("malformed pattern (ends with %)");
            }
            char next = lua_pat[++i]; // Consume next char

            // Handle Lua character classes
            switch (next) {
                case 'a': ss << (in_bracket ? "[:alpha:]" : "[[:alpha:]]"); break;
                case 'c': ss << (in_bracket ? "[:cntrl:]" : "[[:cntrl:]]"); break;
                case 'd': ss << (in_bracket ? "[:digit:]" : "\\d"); break;
                case 'l': ss << (in_bracket ? "[:lower:]" : "[[:lower:]]"); break;
                case 'p': ss << (in_bracket ? "[:punct:]" : "[[:punct:]]"); break;
                case 's': ss << (in_bracket ? "[:space:]" : "\\s"); break;
                case 'u': ss << (in_bracket ? "[:upper:]" : "[[:upper:]]"); break;
                case 'w': ss << (in_bracket ? "[:alnum:]" : "\\w"); break;
                case 'x': ss << (in_bracket ? "[:xdigit:]" : "[[:xdigit:]]"); break;
                case 'z': ss << "\\0"; break; // Null byte
                // Upper case (negation)
                case 'A': ss << (in_bracket ? "^[:alpha:]" : "[^[:alpha:]]"); break;
                case 'C': ss << (in_bracket ? "^[:cntrl:]" : "[^[:cntrl:]]"); break;
                case 'D': ss << (in_bracket ? "^[:digit:]" : "\\D"); break;
                case 'L': ss << (in_bracket ? "^[:lower:]" : "[^[:lower:]]"); break;
                case 'P': ss << (in_bracket ? "^[:punct:]" : "[^[:punct:]]"); break;
                case 'S': ss << (in_bracket ? "^[:space:]" : "\\S"); break;
                case 'U': ss << (in_bracket ? "^[:upper:]" : "[^[:upper:]]"); break;
                case 'W': ss << (in_bracket ? "^[:alnum:]" : "\\W"); break;
                case 'X': ss << (in_bracket ? "^[:xdigit:]" : "[^[:xdigit:]]"); break;
                case 'Z': ss << "[^\\0]"; break;
                case 'b': throw std::runtime_error("'%b' pattern (balanced matching) not supported in this environment");
                case 'f': throw std::runtime_error("'%f' pattern (frontier) not supported in this environment");
                default:
                    // Escape non-alphanumeric literals (e.g. %%, %., %()
                    if (!isalnum(next)) {
                        ss << "\\" << next;
                    } else {
                        // Unknown class, treat as literal
                        ss << next;
                    }
                    break;
            }
        } else if (c == '.') {
            if (in_bracket) ss << ".";
            else ss << "[\\s\\S]"; // Lua dot matches ALL chars, std::regex dot excludes newline
        } else if (c == '-') {
            // Lua non-greedy match '*'
            // In Regex, *? is non-greedy star. But Lua '-' is specifically 0 or more lazy.
            if (in_bracket) ss << "-";
            else ss << "*?"; 
        } else if (c == '[' || c == ']') {
            in_bracket = (c == '[');
            ss << c;
        } else if (strchr("\\|{}?+*", c)) { 
            // Escape regex special chars that are literals in Lua
            if (strchr("|{}\\^$", c)) {
                if (in_bracket) ss << c;
                else ss << "\\" << c;
            } else {
                // * + ? are same in Lua and regex
                ss << c;
            }
        } else {
            ss << c;
        }
    }
    return ss.str();
}

// Transpile Lua replacement string (e.g. "%1 matches") to Regex ("$1 matches")
std::string lua_repl_to_regex(const std::string& lua_repl) {
    std::stringstream ss;
    for (size_t i = 0; i < lua_repl.length(); ++i) {
        if (lua_repl[i] == '%') {
            if (i + 1 >= lua_repl.length()) {
                 ss << "%"; // Trailing %
                 break;
            }
            char next = lua_repl[++i];
            if (next == '%') {
                ss << "%"; // %% -> literal %
            } else if (next == '0') {
                ss << "$&"; // %0 -> whole match -> $&
            } else if (isdigit(next)) {
                ss << "$" << next; // %1 -> $1
            } else {
                ss << "%" << next; // Unknown, keep as is?
            }
        } else {
            if (lua_repl[i] == '$') ss << "\\$"; // Escape regex replacement special
            else ss << lua_repl[i];
        }
    }
    return ss.str();
}

// --- String Library Functions ---

// string.byte
std::vector<LuaValue> string_byte(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    double i_d = !std::holds_alternative<std::monostate>(args->get("2")) ? get_double(args->get("2")) : 1.0;
    double j_d = !std::holds_alternative<std::monostate>(args->get("3")) ? get_double(args->get("3")) : i_d;

    long long i = static_cast<long long>(i_d);
    long long j = static_cast<long long>(j_d);

    long long len = s.length();
    if (i < 0) i = len + i + 1;
    if (j < 0) j = len + j + 1;
    if (i < 1) i = 1;
    if (j > len) j = len;

    if (i > j) return {};

    std::vector<LuaValue> results_vec;
    for (long long k = i - 1; k < j; ++k) {
        results_vec.push_back(static_cast<double>(static_cast<unsigned char>(s[k])));
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
    throw std::runtime_error("string.dump is not supported.");
    return {};
}

// string.find
std::vector<LuaValue> string_find(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::string pattern = get_string(args->get("2"));
    double init_double = !std::holds_alternative<std::monostate>(args->get("3")) ? get_double(args->get("3")) : 1.0;
    bool plain = std::holds_alternative<bool>(args->get("4")) ? std::get<bool>(args->get("4")) : false;

    long long init = static_cast<long long>(init_double);
    long long len = s.length();

    if (init < 0) init = len + init + 1;
    if (init < 1) init = 1;
    
    if (init > len + 1) return {std::monostate{}};
    // Special case: init > len (empty string at end) is handled by substr returning ""

    std::string search_s = (init <= len) ? s.substr(init - 1) : "";

    if (plain) {
        size_t pos = search_s.find(pattern);
        if (pos != std::string::npos) {
            return {static_cast<double>(pos + init), static_cast<double>(pos + init + pattern.length() - 1)};
        }
    } else {
        try {
            std::string cpp_pattern = lua_pattern_to_regex(pattern);
            std::regex re(cpp_pattern);
            std::smatch match;
            if (std::regex_search(search_s, match, re)) {
                double start_idx = static_cast<double>(match.position(0) + init);
                double end_idx = static_cast<double>(match.position(0) + match.length(0) + init - 1);
                
                std::vector<LuaValue> results_vec;
                results_vec.push_back(start_idx);
                results_vec.push_back(end_idx);

                // Captures
                for (size_t i = 1; i < match.size(); ++i) {
                    if (match[i].matched) {
                        results_vec.push_back(match.str(i));
                    } else {
                         // Unmatched optional capture
                        results_vec.push_back(std::monostate{}); 
                    }
                }
                return results_vec;
            }
        } catch (const std::exception& e) {
            throw std::runtime_error("error in pattern: " + std::string(e.what()));
        }
    }
    return {std::monostate{}};
}

// string.format
std::vector<LuaValue> string_format(std::shared_ptr<LuaObject> args) {
    std::string format_str = get_string(args->get("1"));
    std::string result = "";
    int arg_idx = 2;
    size_t i = 0;
    size_t len = format_str.length();

    while (i < len) {
        if (format_str[i] != '%') {
            result += format_str[i++];
            continue;
        }
        i++; // skip %
        if (i >= len) break;

        if (format_str[i] == '%') {
            result += '%';
            i++;
            continue;
        }

        // Parse flags, width, precision
        size_t spec_start = i - 1; // include the %
        while (i < len && (strchr("-+ #0", format_str[i]) || isdigit(format_str[i]) || format_str[i] == '.')) {
            i++;
        }
        if (i >= len) break;

        char spec = format_str[i];
        std::string fmt_spec = format_str.substr(spec_start, i - spec_start + 1);
        i++; // consume specifier

        LuaValue val = args->get(std::to_string(arg_idx++));
        char buffer[4096]; 

        // Simple mapping of Lua types to printf
        switch (spec) {
            case 'c':
                snprintf(buffer, sizeof(buffer), fmt_spec.c_str(), (int)get_double(val));
                break;
            case 'd': case 'i':
                snprintf(buffer, sizeof(buffer), fmt_spec.c_str(), (long long)get_double(val));
                break;
            case 'o': case 'u': case 'x': case 'X':
                snprintf(buffer, sizeof(buffer), fmt_spec.c_str(), (unsigned long long)get_double(val));
                break;
            case 'e': case 'E': case 'f': case 'g': case 'G': case 'a': case 'A':
                snprintf(buffer, sizeof(buffer), fmt_spec.c_str(), get_double(val));
                break;
            case 's': {
                std::string s_val = to_cpp_string(val);
                snprintf(buffer, sizeof(buffer), fmt_spec.c_str(), s_val.c_str());
                break;
            }
            case 'q': {
                std::string s_val = to_cpp_string(val);
                result += '"';
                for (char c : s_val) {
                    if (c == '"') result += "\\\"";
                    else if (c == '\\') result += "\\\\";
                    else if (c == '\n') result += "\\\n";
                    else if (c < 32 || c > 126) {
                        char esc[5]; snprintf(esc, 5, "\\%03d", (unsigned char)c);
                        result += esc;
                    } else {
                        result += c;
                    }
                }
                result += '"';
                buffer[0] = '\0';
                break;
            }
            default:
                throw std::runtime_error("invalid option to 'format'");
        }
        result += buffer;
    }
    return {result};
}

// string.gmatch
std::vector<LuaValue> string_gmatch(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::string lua_pattern = get_string(args->get("2"));
    
    std::shared_ptr<std::regex> re;
    try {
        re = std::make_shared<std::regex>(lua_pattern_to_regex(lua_pattern));
    } catch (const std::exception& e) {
        throw std::runtime_error("bad pattern: " + std::string(e.what()));
    }

    auto it_ptr = std::make_shared<std::sregex_iterator>(s.begin(), s.end(), *re);
    auto end_ptr = std::make_shared<std::sregex_iterator>();

    auto func = [it_ptr, end_ptr](std::shared_ptr<LuaObject> _) -> std::vector<LuaValue> {
        if (*it_ptr == *end_ptr) return {}; // nil

        std::smatch match = **it_ptr;
        (*it_ptr)++;

        std::vector<LuaValue> res;
        if (match.size() > 1) {
            for (size_t k = 1; k < match.size(); ++k) res.push_back(match.str(k));
        } else {
            res.push_back(match.str(0));
        }
        return res;
    };

    return {std::make_shared<LuaFunctionWrapper>(func)};
}

// string.gsub
std::vector<LuaValue> string_gsub(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::string lua_pattern = get_string(args->get("2"));
    LuaValue repl_val = args->get("3");
    double n_d = !std::holds_alternative<std::monostate>(args->get("4")) ? get_double(args->get("4")) : -1.0;
    long long max_subs = (n_d < 0) ? (s.length() + 1) : (long long)n_d;

    std::regex re;
    try {
        re = std::regex(lua_pattern_to_regex(lua_pattern));
    } catch (const std::exception& e) {
        throw std::runtime_error("bad pattern: " + std::string(e.what()));
    }

    std::string result;
    auto it = std::sregex_iterator(s.begin(), s.end(), re);
    auto end = std::sregex_iterator();
    size_t last_pos = 0;
    long long matches = 0;

    for (; it != end && matches < max_subs; ++it) {
        std::smatch match = *it;
        result.append(s, last_pos, match.position(0) - last_pos);
        
        if (std::holds_alternative<std::string>(repl_val)) {
            // String replacement with % capture support
            std::string raw_repl = std::get<std::string>(repl_val);
            std::string cpp_repl = lua_repl_to_regex(raw_repl);
            result.append(match.format(cpp_repl)); 
        } 
        else if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(repl_val)) {
            // Function replacement
            auto func = std::get<std::shared_ptr<LuaFunctionWrapper>>(repl_val);
            auto f_args = std::make_shared<LuaObject>();
            
            if (match.size() > 1) {
                for(size_t k=1; k<match.size(); ++k) f_args->set(std::to_string(k), match.str(k));
            } else {
                f_args->set("1", match.str(0));
            }
            
            auto res = func->func(f_args);
            if (!res.empty() && !std::holds_alternative<std::monostate>(res[0])) {
                result += to_cpp_string(res[0]);
            } // else match is removed (empty string appended)
        }
        else if (std::holds_alternative<std::shared_ptr<LuaObject>>(repl_val)) {
            // Table replacement
            auto tbl = std::get<std::shared_ptr<LuaObject>>(repl_val);
            std::string key = (match.size() > 1) ? match.str(1) : match.str(0);
            LuaValue val = tbl->get(key);
            if (!std::holds_alternative<std::monostate>(val)) {
                result += to_cpp_string(val);
            } else {
                result += match.str(0); // Keep original if not in table
            }
        }
        else {
            throw std::runtime_error("invalid replacement type");
        }

        last_pos = match.position(0) + match.length(0);
        matches++;
        
        // Prevent infinite loop on empty matches
        if (match.length(0) == 0) {
            if (last_pos < s.length()) {
                result += s[last_pos];
                last_pos++;
            }
        }
    }
    result.append(s, last_pos, std::string::npos);

    return {result, static_cast<double>(matches)};
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
    std::string lua_pattern = get_string(args->get("2"));
    double init_d = !std::holds_alternative<std::monostate>(args->get("3")) ? get_double(args->get("3")) : 1.0;
    
    long long init = (long long)init_d;
    if(init < 1) init = 1; 
    if(init > s.length() + 1) return {std::monostate{}};

    std::string search_s = s.substr(init - 1);
    
    try {
        std::regex re(lua_pattern_to_regex(lua_pattern));
        std::smatch match;
        if(std::regex_search(search_s, match, re)) {
            std::vector<LuaValue> res;
            if(match.size() > 1) {
                for(size_t k=1; k<match.size(); ++k) res.push_back(match.str(k));
            } else {
                res.push_back(match.str(0));
            }
            return res;
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("bad pattern: " + std::string(e.what()));
    }

    return {std::monostate{}};
}

// string.pack (Stub)
std::vector<LuaValue> string_pack(std::shared_ptr<LuaObject> args) { throw std::runtime_error("not implemented"); }
// string.packsize (Stub)
std::vector<LuaValue> string_packsize(std::shared_ptr<LuaObject> args) { throw std::runtime_error("not implemented"); }

// string.rep
std::vector<LuaValue> string_rep(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    double n = get_double(args->get("2"));
    std::string sep = std::holds_alternative<std::string>(args->get("3")) ? std::get<std::string>(args->get("3")) : "";
    
    if (n <= 0) return {""};
    std::string res;
    // Simple optimization: pre-reserve approximate size
    res.reserve((s.length() + sep.length()) * (size_t)n);
    
    for(long long i=0; i<(long long)n; ++i) {
        if(i > 0) res += sep;
        res += s;
    }
    return {res};
}

// string.reverse
std::vector<LuaValue> string_reverse(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::reverse(s.begin(), s.end());
    return {s};
}

// --- C++ Helper Implementations ---

std::vector<LuaValue> lua_string_match(const LuaValue& str, const LuaValue& pattern) {
    std::string s = get_string(str);
    std::string lua_pattern = get_string(pattern);
    try {
        std::regex re(lua_pattern_to_regex(lua_pattern));
        std::smatch match;
        if (std::regex_search(s, match, re)) {
            if (match.size() > 1) {
                return {std::string(match[1])};
            }
            return {std::string(match[0])};
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("bad pattern: " + std::string(e.what()));
    }
    return {std::monostate{}};
}

std::vector<LuaValue> lua_string_find(const LuaValue& str, const LuaValue& pattern) {
    std::string s = get_string(str);
    std::string lua_pattern = get_string(pattern);
    try {
        std::regex re(lua_pattern_to_regex(lua_pattern));
        std::smatch match;
        if (std::regex_search(s, match, re)) {
             long long start = match.position(0) + 1;
             long long end = match.position(0) + match.length(0);
             return {static_cast<double>(start), static_cast<double>(end)};
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("error in pattern: " + std::string(e.what()));
    }
    return {std::monostate{}};
}

std::vector<LuaValue> lua_string_gsub(const LuaValue& str, const LuaValue& pattern, const LuaValue& replacement) {
    std::string s = get_string(str);
    std::string lua_pattern = get_string(pattern);
    
    try {
        std::regex re(lua_pattern_to_regex(lua_pattern));
        
        if (std::holds_alternative<std::string>(replacement)) {
            std::string r = std::get<std::string>(replacement);
            std::string cpp_repl = lua_repl_to_regex(r);
            return {std::regex_replace(s, re, cpp_repl)};
        } else if (std::holds_alternative<double>(replacement)) {
             // Handle number as string replacement
             std::string r = to_cpp_string(replacement);
             std::string cpp_repl = lua_repl_to_regex(r);
             return {std::regex_replace(s, re, cpp_repl)};
        }
        
        // For function/table replacement, we'd need more complex logic similar to string_gsub.
        // But for now, the inline implementation only supported string replacement anyway.
        // And cpp_translator.lua only uses string replacement.
        throw std::runtime_error("lua_string_gsub only supports string replacement for now");
        
    } catch (const std::exception& e) {
        throw std::runtime_error("bad pattern: " + std::string(e.what()));
    }
}

// string.sub
std::vector<LuaValue> string_sub(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    double i_d = !std::holds_alternative<std::monostate>(args->get("2")) ? get_double(args->get("2")) : 1.0;
    double j_d = !std::holds_alternative<std::monostate>(args->get("3")) ? get_double(args->get("3")) : -1.0;

    long long len = s.length();
    long long i = (long long)i_d;
    long long j = (long long)j_d;

    if (i < 0) i = len + i + 1;
    if (j < 0) j = len + j + 1;
    if (i < 1) i = 1;
    if (j > len) j = len;

    if (i <= j) return {s.substr(i-1, j-i+1)};
    return {""};
}

// string.unpack (Stub)
std::vector<LuaValue> string_unpack(std::shared_ptr<LuaObject> args) { throw std::runtime_error("not implemented"); }

// string.upper
std::vector<LuaValue> string_upper(std::shared_ptr<LuaObject> args) {
    std::string s = get_string(args->get("1"));
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return {s};
}

// --- Library Creation ---

std::shared_ptr<LuaObject> create_string_library() {
    auto lib = std::make_shared<LuaObject>();
    lib->properties = {
        {"byte", std::make_shared<LuaFunctionWrapper>(string_byte)},
        {"char", std::make_shared<LuaFunctionWrapper>(string_char)},
        {"dump", std::make_shared<LuaFunctionWrapper>(string_dump)},
        {"find", std::make_shared<LuaFunctionWrapper>(string_find)},
        {"format", std::make_shared<LuaFunctionWrapper>(string_format)},
        {"gmatch", std::make_shared<LuaFunctionWrapper>(string_gmatch)},
        {"gsub", std::make_shared<LuaFunctionWrapper>(string_gsub)},
        {"len", std::make_shared<LuaFunctionWrapper>(string_len)},
        {"lower", std::make_shared<LuaFunctionWrapper>(string_lower)},
        {"match", std::make_shared<LuaFunctionWrapper>(string_match)},
        {"pack", std::make_shared<LuaFunctionWrapper>(string_pack)},
        {"packsize", std::make_shared<LuaFunctionWrapper>(string_packsize)},
        {"rep", std::make_shared<LuaFunctionWrapper>(string_rep)},
        {"reverse", std::make_shared<LuaFunctionWrapper>(string_reverse)},
        {"sub", std::make_shared<LuaFunctionWrapper>(string_sub)},
        {"unpack", std::make_shared<LuaFunctionWrapper>(string_unpack)},
        {"upper", std::make_shared<LuaFunctionWrapper>(string_upper)}
    };
    return lib;
}