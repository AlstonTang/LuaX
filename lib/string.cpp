#include "string.hpp"
#include "lua_object.hpp"
#include <cstring>
#include <algorithm>
#include <vector>
#include <numeric>
#include <stdexcept>
#include <cstdio>
#include <cctype>
#include <sstream>
#include <map>

// --- Lua Pattern Matching Engine ---

namespace LuaPattern {

	constexpr int LUA_MAXCAPTURES = 32;
	constexpr int CAP_UNFINISHED = -1;
	constexpr int CAP_POSITION = -2;

	struct MatchState {
		const char* src_init;
		const char* src_end;
		const char* p_end;
		int level; // Total number of captures
		struct {
			const char* init;
			ptrdiff_t len;
		} capture[LUA_MAXCAPTURES];
	};

	// Helper to check characters safely
	static bool check_class(int c, int cl) {
		int res;
		switch (tolower(cl)) {
			case 'a': res = isalpha(c); break;
			case 'c': res = iscntrl(c); break;
			case 'd': res = isdigit(c); break;
			case 'g': res = isgraph(c); break;
			case 'l': res = islower(c); break;
			case 'p': res = ispunct(c); break;
			case 's': res = isspace(c); break;
			case 'u': res = isupper(c); break;
			case 'w': res = isalnum(c); break;
			case 'x': res = isxdigit(c); break;
			case 'z': res = (c == 0); break;  // deprecated option
			default: return (cl == c);
		}
		return (islower(cl) ? res : !res);
	}

	static bool match_class(int c, int cl) {
		return (tolower(cl) == 'z' && c == 0) || check_class(c, cl);
	}

	static bool match_bracket_class(int c, const char* p, const char* ec) {
		bool sig = true;
		if (*(p + 1) == '^') {
			sig = false;
			p++;
		}
		while (++p < ec) {
			if (*p == '%') {
				p++;
				if (match_class(c, *p)) return sig;
			}
			else if ((*(p + 1) == '-') && (p + 2 < ec)) {
				p += 2;
				if (*(p - 2) <= c && c <= *p) return sig;
			}
			else if (*p == c) return sig;
		}
		return !sig;
	}

	static bool singlematch(MatchState* ms, const char* s, const char* p, const char* ep) {
		if (s >= ms->src_end) return false;
		int c = static_cast<unsigned char>(*s);
		switch (*p) {
			case '.': return true; // Matches any char
			case '%': return match_class(c, *(p + 1));
			case '[': return match_bracket_class(c, p, ep - 1);
			default: return (*p == c);
		}
	}

	static const char* match(MatchState* ms, const char* s, const char* p);

	static const char* matchbalance(MatchState* ms, const char* s, const char* p) {
		if (p >= ms->p_end - 1) throw std::runtime_error("malformed pattern (missing arguments to '%b')");
		if (s >= ms->src_end) return nullptr;
		char b = *p;
		char e = *(p + 1);
		if (*s != b) return nullptr;
		int cont = 1;
		while (++s < ms->src_end) {
			if (*s == e) {
				if (--cont == 0) return s + 1;
			}
			else if (*s == b) cont++;
		}
		return nullptr; // string ends with unbalanced
	}

	static const char* max_expand(MatchState* ms, const char* s, const char* p, const char* ep) {
		ptrdiff_t i = 0;
		while (singlematch(ms, s + i, p, ep)) i++;
		// Keeps trying to match the rest of the pattern, shrinking the expansion
		while (i >= 0) {
			const char* res = match(ms, s + i, ep + 1);
			if (res) return res;
			i--;
		}
		return nullptr;
	}

	static const char* min_expand(MatchState* ms, const char* s, const char* p, const char* ep) {
		for (;;) {
			const char* res = match(ms, s, ep + 1);
			if (res) return res;
			if (singlematch(ms, s, p, ep)) s++;
			else return nullptr;
		}
	}

	static const char* start_capture(MatchState* ms, const char* s, const char* p, int what) {
		if (ms->level >= LUA_MAXCAPTURES) throw std::runtime_error("too many captures");
		ms->capture[ms->level].init = s;
		ms->capture[ms->level].len = what;
		ms->level++;
		const char* res = match(ms, s, p);
		if (res == nullptr) ms->level--; // undo capture
		return res;
	}

	static const char* end_capture(MatchState* ms, const char* s, const char* p) {
		int l = -1;
		// find most recent unfinished capture
		for (l = ms->level - 1; l >= 0; l--) {
			if (ms->capture[l].len == CAP_UNFINISHED) break;
		}
		if (l < 0) throw std::runtime_error("invalid pattern capture");
		ms->capture[l].len = s - ms->capture[l].init;
		const char* res = match(ms, s, p);
		if (res == nullptr) ms->capture[l].len = CAP_UNFINISHED; // undo
		return res;
	}

	static const char* match_capture(MatchState* ms, const char* s, int l) {
		l = l - '1';
		if (l < 0 || l >= ms->level || ms->capture[l].len == CAP_UNFINISHED)
			throw std::runtime_error("invalid capture index");
		size_t len = ms->capture[l].len;
		if ((size_t)(ms->src_end - s) >= len && memcmp(ms->capture[l].init, s, len) == 0)
			return s + len;
		return nullptr;
	}

	static const char* classend(MatchState* ms, const char* p) {
		switch (*p++) {
			case '%':
				if (p == ms->p_end) throw std::runtime_error("malformed pattern (ends with %)");
				return p + 1;
			case '[':
				if (*p == '^') p++;
				do {  // look for a ']'
					if (p == ms->p_end) throw std::runtime_error("malformed pattern (missing ']')");
					if (*(p++) == '%') {
						if (p < ms->p_end) p++;
					}
				} while (*p != ']');
				return p + 1;
			default:
				return p;
		}
	}

	static const char* match(MatchState* ms, const char* s, const char* p) {
		// init: 
		// p is current pattern ptr
		// s is current string ptr
		if (p == ms->p_end) return s; // End of pattern, match succeeded

		switch (*p) {
			case '(':
				if (*(p + 1) == ')') return start_capture(ms, s, p + 2, CAP_POSITION);
				else return start_capture(ms, s, p + 1, CAP_UNFINISHED);
			case ')':
				return end_capture(ms, s, p + 1);
			case '$':
				if ((p + 1) == ms->p_end) // anchor only if at very end
					return (s == ms->src_end) ? s : nullptr;
				// fallthrough (treat '$' as literal if not at end? Lua 5.4 treats strictly)
				break; 
			case '%':
				switch (*(p + 1)) {
					case 'b': return matchbalance(ms, s, p + 2);
					case 'f': {
						p += 2;
						if (*p != '[') throw std::runtime_error("missing '[' after '%f' in pattern");
						const char* ep = classend(ms, p); // points to char after ']'
						char previous = (s == ms->src_init) ? '\0' : *(s - 1);
						// frontier: match if previous not in set, current in set
						// Note: ep-1 points to ']'
						if (match_bracket_class(static_cast<unsigned char>(previous), p, ep - 1) ||
						   !match_bracket_class(static_cast<unsigned char>(*s), p, ep - 1)) return nullptr;
						return match(ms, s, ep);
					}
					case '0': case '1': case '2': case '3': case '4':
					case '5': case '6': case '7': case '8': case '9':
						return match_capture(ms, s, *(p + 1));
					default: break; // standard class/literal
				}
				break;
			default: break;
		}

		const char* ep = classend(ms, p); // points to char after the class/literal
		bool m = (s < ms->src_end) && singlematch(ms, s, p, ep);
		
		switch (*ep) {
			case '?': // 0 or 1
				{
					const char* res;
					if (m && ((res = match(ms, s + 1, ep + 1)) != nullptr)) return res;
					return match(ms, s, ep + 1);
				}
			case '*': // 0 or more greedy
				return max_expand(ms, s, p, ep);
			case '+': // 1 or more greedy
				return (m ? max_expand(ms, s + 1, p, ep) : nullptr);
			case '-': // 0 or more lazy
				return min_expand(ms, s, p, ep);
			default: // single match
				if (!m) return nullptr;
				return match(ms, s + 1, ep);
		}
	}
}

// --- Helper Functions ---

std::string get_string(const LuaValue& v) {
	if (std::holds_alternative<std::string>(v)) {
		return std::get<std::string>(v);
	} else if (std::holds_alternative<double>(v)) {
		double d = std::get<double>(v);
		char buf[64];
		snprintf(buf, 64, "%.14g", d);
		return std::string(buf);
	}
	return "";
}

// Helper to extract captures from a completed MatchState
std::vector<LuaValue> get_captures(const LuaPattern::MatchState& ms, const std::string& s) {
	std::vector<LuaValue> results;
	int nlevels = (ms.level == 0 && ms.capture[0].len != LuaPattern::CAP_UNFINISHED) ? 1 : ms.level;
	
	// If no explicit captures, return the whole match
	if (ms.level == 0) {
		// usually the caller handles the whole match return if level==0
		return {}; 
	}

	for (int i = 0; i < nlevels; ++i) {
		ptrdiff_t len = ms.capture[i].len;
		if (len == LuaPattern::CAP_POSITION) {
			results.push_back(static_cast<double>((ms.capture[i].init - ms.src_init) + 1));
		} else {
			results.push_back(std::string(ms.capture[i].init, len));
		}
	}
	return results;
}

// --- Library Functions ---

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

// string.dump
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
	
	// In Lua, if init > len + 1, it fails.
	if (init > len + 1) return {std::monostate{}};

	if (plain) {
		size_t pos = s.find(pattern, init - 1);
		if (pos != std::string::npos) {
			return {static_cast<double>(pos + 1), static_cast<double>(pos + pattern.length())};
		}
	} else {
		const char* s_ptr = s.c_str();
		const char* s_end = s_ptr + len;
		const char* p_ptr = pattern.c_str();
		const char* p_end = p_ptr + pattern.length();
		const char* search_start = s_ptr + (init - 1);

		bool anchor = (*p_ptr == '^');
		if (anchor) p_ptr++; // skip anchor

		const char* curr = search_start;
		do {
			LuaPattern::MatchState ms;
			ms.src_init = s_ptr;
			ms.src_end = s_end;
			ms.p_end = p_end;
			ms.level = 0;
			
			const char* res = LuaPattern::match(&ms, curr, p_ptr);
			if (res) {
				// Match found
				double start_idx = static_cast<double>(curr - s_ptr + 1);
				double end_idx = static_cast<double>(res - s_ptr);
				
				std::vector<LuaValue> results;
				results.push_back(start_idx);
				results.push_back(end_idx);

				if (ms.level > 0) {
					std::vector<LuaValue> caps = get_captures(ms, s);
					results.insert(results.end(), caps.begin(), caps.end());
				}
				return results;
			}
			if (anchor) break; // if anchored, only try once
		} while (curr++ < s_end);
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

		size_t spec_start = i - 1; 
		while (i < len && (strchr("-+ #0", format_str[i]) || isdigit(format_str[i]) || format_str[i] == '.')) {
			i++;
		}
		if (i >= len) break;

		char spec = format_str[i];
		std::string fmt_spec = format_str.substr(spec_start, i - spec_start + 1);
		i++; 

		LuaValue val = args->get(std::to_string(arg_idx++));
		char buffer[4096]; 

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
	std::string pattern = get_string(args->get("2"));

	auto s_ptr = std::make_shared<std::string>(s);
	auto p_ptr = std::make_shared<std::string>(pattern);
	auto pos_ptr = std::make_shared<size_t>(0); // Current search index

	auto func = [s_ptr, p_ptr, pos_ptr](std::shared_ptr<LuaObject> _) -> std::vector<LuaValue> {
		const char* s_raw = s_ptr->c_str();
		const char* s_end = s_raw + s_ptr->length();
		const char* p_raw = p_ptr->c_str();
		const char* p_end = p_raw + p_ptr->length();
		
		bool anchor = (*p_raw == '^');
		const char* p_eff = anchor ? p_raw + 1 : p_raw;

		const char* curr = s_raw + *pos_ptr;
		
		while (curr <= s_end) { // Allow matching empty string at end
			LuaPattern::MatchState ms;
			ms.src_init = s_raw;
			ms.src_end = s_end;
			ms.p_end = p_end;
			ms.level = 0;

			const char* res = LuaPattern::match(&ms, curr, p_eff);
			if (res) {
				size_t match_len = res - curr;
				// Next search starts after this match. 
				// If match is empty, we must advance 1 char to avoid infinite loop.
				size_t new_pos = (res - s_raw);
				if (match_len == 0 && new_pos < s_ptr->length()) new_pos++;
				
				*pos_ptr = new_pos;
				
				if (anchor && *pos_ptr > 0) {
					 // Anchor matched once, subsequent calls return nil
					 *pos_ptr = s_ptr->length() + 2; 
				}

				if (ms.level > 0) {
					return get_captures(ms, *s_ptr);
				} else {
					return {std::string(curr, match_len)};
				}
			}
			if (anchor) break;
			if (curr == s_end) break;
			curr++;
		}
		return {}; // nil
	};

	return {std::make_shared<LuaFunctionWrapper>(func), std::monostate{}, std::monostate{}};
}

// string.gsub
std::vector<LuaValue> string_gsub(std::shared_ptr<LuaObject> args) {
	std::string s = get_string(args->get("1"));
	std::string pattern = get_string(args->get("2"));
	LuaValue repl_val = args->get("3");
	double n_d = !std::holds_alternative<std::monostate>(args->get("4")) ? get_double(args->get("4")) : -1.0;
	long long max_subs = (n_d < 0) ? (long long)(s.length() + 1) : (long long)n_d;

	std::string result;
	long long matches = 0;

	const char* s_raw = s.c_str();
	const char* s_end = s_raw + s.length();
	const char* p_raw = pattern.c_str();
	const char* p_end = p_raw + pattern.length();
	
	bool anchor = (*p_raw == '^');
	const char* p_eff = anchor ? p_raw + 1 : p_raw;

	const char* curr = s_raw;
	const char* last_match_end = s_raw;

	while (curr <= s_end && matches < max_subs) {
		LuaPattern::MatchState ms;
		ms.src_init = s_raw;
		ms.src_end = s_end;
		ms.p_end = p_end;
		ms.level = 0;

		const char* res = LuaPattern::match(&ms, curr, p_eff);
		if (res) {
			// Append non-matched part
			result.append(last_match_end, curr - last_match_end);
			
			// Prepare captures
			std::vector<LuaValue> caps;
			if (ms.level == 0) {
				caps.push_back(std::string(curr, res - curr)); // Capture 0 is whole match
			} else {
				// If using table/function, cap 0 is whole match for 'table' key but usually implicit
				// Lua rules:
				// String repl: %0 is whole match, %1 is cap 1.
				// Func: args are cap1...capN. If no caps, arg is whole match.
				// Table: key is cap1 (or whole match).
				caps.push_back(std::string(curr, res - curr)); // Push whole match as index 0 for reference
				std::vector<LuaValue> real_caps = get_captures(ms, s);
				caps.insert(caps.end(), real_caps.begin(), real_caps.end());
			}

			// Determine Replacement
			std::string replacement_str;
			bool has_rep = false;

			if (std::holds_alternative<std::string>(repl_val) || std::holds_alternative<double>(repl_val)) {
				std::string repl_tmpl = to_cpp_string(repl_val);
				for (size_t i = 0; i < repl_tmpl.length(); ++i) {
					if (repl_tmpl[i] == '%') {
						i++;
						if (i >= repl_tmpl.length()) { replacement_str += '%'; break; }
						char c = repl_tmpl[i];
						if (c == '%') replacement_str += '%';
						else if (isdigit(c)) {
							int idx = c - '0';
							if (idx < (int)caps.size()) replacement_str += to_cpp_string(caps[idx]);
							 // else invalid capture, ignore or error? Lua ignores
						} else {
							replacement_str += '%'; replacement_str += c;
						}
					} else {
						replacement_str += repl_tmpl[i];
					}
				}
				has_rep = true;
			} else if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(repl_val)) {
				auto func = std::get<std::shared_ptr<LuaFunctionWrapper>>(repl_val);
				auto f_args = std::make_shared<LuaObject>();
				// If we have explicit captures (caps.size() > 1 since caps[0] is whole), use them
				// otherwise use caps[0] (whole match)
				if (caps.size() > 1) {
					for (size_t k = 1; k < caps.size(); ++k) f_args->set(std::to_string(k), caps[k]);
				} else {
					f_args->set("1", caps[0]);
				}
				auto f_res = func->func(f_args);
				if (!f_res.empty() && !std::holds_alternative<std::monostate>(f_res[0])) {
					replacement_str = to_cpp_string(f_res[0]);
					has_rep = true;
				}
			} else if (std::holds_alternative<std::shared_ptr<LuaObject>>(repl_val)) {
				auto tbl = std::get<std::shared_ptr<LuaObject>>(repl_val);
				// Key is cap 1 if exists, else cap 0
				std::string key = (caps.size() > 1) ? to_cpp_string(caps[1]) : to_cpp_string(caps[0]);
				LuaValue val = tbl->get(key);
				if (!std::holds_alternative<std::monostate>(val)) {
					replacement_str = to_cpp_string(val);
					has_rep = true;
				}
			}

			if (!has_rep) {
				// If function/table returned nil/false (or monostate), keep original match
				replacement_str = std::string(curr, res - curr);
			}

			result += replacement_str;
			matches++;
			last_match_end = res;

			if (res == curr) {
				 // Empty match, advance 1 to avoid loop
				 if (curr < s_end) {
					 result += *curr; // Append char at curr since we skipped it in matching logic?
					 // Wait, if match is empty, we effectively matched at the boundary.
					 // The loop condition needs care.
					 // Lua logic: if empty match, copy one char and retry.
					 // But we already appended up to `curr`.
					 // The `last_match_end` is now `curr`.
					 // We need to consume one char from source if we aren't at end.
					 last_match_end++; 
					 curr++;
				 } else {
					 break; // EOF
				 }
			} else {
				curr = res;
			}
			if (anchor) break;
		} else {
			if (curr < s_end) curr++;
			else break;
		}
	}
	result.append(last_match_end, s_end - last_match_end);
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
	std::string pattern = get_string(args->get("2"));
	double init_d = !std::holds_alternative<std::monostate>(args->get("3")) ? get_double(args->get("3")) : 1.0;
	
	long long init = (long long)init_d;
	if(init < 0) init = s.length() + init + 1;
	if(init < 1) init = 1; 
	if(init > s.length() + 1) return {std::monostate{}};

	const char* s_raw = s.c_str();
	const char* s_end = s_raw + s.length();
	const char* p_raw = pattern.c_str();
	const char* p_end = p_raw + pattern.length();
	
	bool anchor = (*p_raw == '^');
	const char* p_eff = anchor ? p_raw + 1 : p_raw;

	const char* curr = s_raw + (init - 1);

	do {
		LuaPattern::MatchState ms;
		ms.src_init = s_raw;
		ms.src_end = s_end;
		ms.p_end = p_end;
		ms.level = 0;
		
		const char* res = LuaPattern::match(&ms, curr, p_eff);
		if (res) {
			if (ms.level > 0) {
				return get_captures(ms, s);
			} else {
				return {std::string(curr, res - curr)};
			}
		}
		if (anchor) break;
	} while (curr++ < s_end);

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

// --- C++ Helper Implementations (Updated to use native matcher) ---

std::vector<LuaValue> lua_string_match(const LuaValue& str, const LuaValue& pattern) {
	std::shared_ptr<LuaObject> args = std::make_shared<LuaObject>();
	args->set("1", str);
	args->set("2", pattern);
	return string_match(args);
}

std::vector<LuaValue> lua_string_find(const LuaValue& str, const LuaValue& pattern) {
	std::shared_ptr<LuaObject> args = std::make_shared<LuaObject>();
	args->set("1", str);
	args->set("2", pattern);
	return string_find(args);
}

std::vector<LuaValue> lua_string_gsub(const LuaValue& str, const LuaValue& pattern, const LuaValue& replacement) {
	std::shared_ptr<LuaObject> args = std::make_shared<LuaObject>();
	args->set("1", str);
	args->set("2", pattern);
	args->set("3", replacement);
	return string_gsub(args);
}

// --- Library Creation ---

std::shared_ptr<LuaObject> create_string_library() {
	static std::shared_ptr<LuaObject> lib;
	if (lib) return lib;
	
	lib = std::make_shared<LuaObject>();
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

	// Metatable to allow string methods on string values (handled in LuaObject::get_item or global logic usually,
	// but here we just return the library table. The metatable for string primitives is set globally or handled via special case logic.)
	// Actually, Lua 5.4 sets the string metatable to this library table itself usually, or the __index of the string metatable points here.
	// For now, we just return the library.

	return lib;
}