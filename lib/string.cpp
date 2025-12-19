#include "string.hpp"
#include "lua_object.hpp"
#include <cstring>
#include <algorithm>
#include <vector>
#include <stdexcept>
#include <cstdio>
#include <cctype>
#include <sstream>
#include <charconv>

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
		case 'a': res = isalpha(c);
			break;
		case 'c': res = iscntrl(c);
			break;
		case 'd': res = isdigit(c);
			break;
		case 'g': res = isgraph(c);
			break;
		case 'l': res = islower(c);
			break;
		case 'p': res = ispunct(c);
			break;
		case 's': res = isspace(c);
			break;
		case 'u': res = isupper(c);
			break;
		case 'w': res = isalnum(c);
			break;
		case 'x': res = isxdigit(c);
			break;
		case 'z': res = (c == 0);
			break;
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

	static bool singlematch(const MatchState* ms, const char* s, const char* p, const char* ep) {
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

	static const char* matchbalance(const MatchState* ms, const char* s, const char* p) {
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
			if (const char* res = match(ms, s + i, ep + 1)) return res;
			i--;
		}
		return nullptr;
	}

	static const char* min_expand(MatchState* ms, const char* s, const char* p, const char* ep) {
		while (true) {
			if (const char* res = match(ms, s, ep + 1)) return res;
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
		int l = ms->level - 1;
		// find most recent unfinished capture
		while (l >= 0) {
			if (ms->capture[l].len == CAP_UNFINISHED) break;
			--l;
		}
		if (l < 0) throw std::runtime_error("invalid pattern capture");
		ms->capture[l].len = s - ms->capture[l].init;
		const char* res = match(ms, s, p);
		if (res == nullptr) ms->capture[l].len = CAP_UNFINISHED; // undo
		return res;
	}

	static const char* match_capture(const MatchState* ms, const char* s, int l) {
		l = l - '1';
		if (l < 0 || l >= ms->level || ms->capture[l].len == CAP_UNFINISHED)
			throw std::runtime_error("invalid capture index");
		size_t len = ms->capture[l].len;
		if (static_cast<size_t>(ms->src_end - s) >= len && memcmp(ms->capture[l].init, s, len) == 0)
			return s + len;
		return nullptr;
	}

	static const char* classend(const MatchState* ms, const char* p) {
		switch (*p++) {
		case '%':
			if (p == ms->p_end) throw std::runtime_error("malformed pattern (ends with %)");
			return p + 1;
		case '[':
			if (*p == '^') p++;
			do { // look for a ']'
				if (p == ms->p_end) throw std::runtime_error("malformed pattern (missing ']')");
				if (*(p++) == '%') {
					if (p < ms->p_end) p++;
				}
			}
			while (*p != ']');
			return p + 1;
		default:
			return p;
		}
	}

	static const char* match(MatchState* ms, const char* s, const char* p) {
		if (p == ms->p_end) return s;

		switch (*p) {
		case '(':
			if (*(p + 1) == ')') return start_capture(ms, s, p + 2, CAP_POSITION);
			else return start_capture(ms, s, p + 1, CAP_UNFINISHED);
		case ')':
			return end_capture(ms, s, p + 1);
		case '$':
			if ((p + 1) == ms->p_end)
				return (s == ms->src_end) ? s : nullptr;
			break;
		case '%':
			switch (*(p + 1)) {
			case 'b': return matchbalance(ms, s, p + 2);
			case 'f': {
				p += 2;
				if (*p != '[') throw std::runtime_error("missing '[' after '%f' in pattern");
				const char* ep = classend(ms, p);
				char previous = (s == ms->src_init) ? '\0' : *(s - 1);
				if (match_bracket_class(static_cast<unsigned char>(previous), p, ep - 1) ||
					!match_bracket_class(static_cast<unsigned char>(*s), p, ep - 1))
					return nullptr;
				return match(ms, s, ep);
			}
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				return match_capture(ms, s, *(p + 1));
			default: break;
			}
			break;
		default: break;
		}

		const char* ep = classend(ms, p);
		bool m = (s < ms->src_end) && singlematch(ms, s, p, ep);

		switch (*ep) {
		case '?': {
			const char* res;
			if (m && ((res = match(ms, s + 1, ep + 1)) != nullptr)) return res;
			return match(ms, s, ep + 1);
		}
		case '*':
			return max_expand(ms, s, p, ep);
		case '+':
			return (m ? max_expand(ms, s + 1, p, ep) : nullptr);
		case '-':
			return min_expand(ms, s, p, ep);
		default:
			if (!m) return nullptr;
			return match(ms, s + 1, ep);
		}
	}
}

// --- Helper Functions ---

std::string get_string(const LuaValue& v) {
	return std::visit([](auto&& arg) -> std::string {
		using T = std::decay_t<decltype(arg)>;

		if constexpr (std::is_same_v<T, std::string>) {
			return arg; // Copy the existing string
		}
		else if constexpr (std::is_same_v<T, double>) {
			char buf[32];
			// std::to_chars is significantly faster than snprintf
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), arg, std::chars_format::general, 14);
			return std::string(buf, ptr - buf);
		}
		else if constexpr (std::is_same_v<T, long long>) {
			char buf[24];
			// std::to_chars is significantly faster than std::to_string
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), arg);
			return std::string(buf, ptr - buf);
		}
		else {
			return "";
		}
	}, v);
}

// Helper to extract captures directly into the output vector
void get_captures(const LuaPattern::MatchState& ms, std::vector<LuaValue>& out) {
	int nlevels = (ms.level == 0 && ms.capture[0].len != LuaPattern::CAP_UNFINISHED) ? 1 : ms.level;

	// If no explicit captures, the caller usually pushes the whole match manually if needed.
	// But in string.match, if level == 0, we return the whole match.
	// This function populates *captures* specifically.

	for (int i = 0; i < nlevels; ++i) {
		ptrdiff_t len = ms.capture[i].len;
		if (len == LuaPattern::CAP_POSITION) {
			out.push_back(static_cast<double>((ms.capture[i].init - ms.src_init) + 1));
		}
		else {
			out.push_back(std::string(ms.capture[i].init, len));
		}
	}
}

// --- Library Functions ---

// string.byte
void string_byte(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string s = get_string(args[0]);
	double i_d = n_args >= 2 ? get_double(args[1]) : 1.0;
	double j_d = n_args >= 3 ? get_double(args[2]) : i_d;

	long long i = static_cast<long long>(i_d);
	long long j = static_cast<long long>(j_d);

	long long len = s.length();
	if (i < 0) i = len + i + 1;
	if (j < 0) j = len + j + 1;
	if (i < 1) i = 1;
	if (j > len) j = len;

	out.clear();
	if (i > j) return;

	for (long long k = i - 1; k < j; ++k) {
		out.push_back(static_cast<double>(static_cast<unsigned char>(s[k])));
	}
}

// string.char
void string_char(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string result_str = "";
	for (size_t i = 0; i < n_args; ++i) {
		result_str += static_cast<char>(static_cast<int>(get_double(args[i])));
	}
	out.assign({result_str});
}

// string.dump
void string_dump(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	// Not supported in this interpreter
	out.clear();
}

// string.find
void string_find(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string s = get_string(args[0]);
	std::string pattern = get_string(args[1]);
	double init_double = n_args >= 3 ? get_double(args[2]) : 1.0;
	bool plain = n_args >= 4 && std::holds_alternative<bool>(args[3]) ? std::get<bool>(args[3]) : false;

	long long init = static_cast<long long>(init_double);
	long long len = s.length();

	if (init < 0) init = len + init + 1;
	if (init < 1) init = 1;

	if (init > len + 1) {
		out.assign({std::monostate{}});
		return;
	}

	if (plain) {
		size_t pos = s.find(pattern, init - 1);
		if (pos != std::string::npos) {
			out.assign({static_cast<long long>(pos + 1), static_cast<long long>(pos + pattern.length())});
			return;
		}
	}
	else {
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

			if (const char* res = LuaPattern::match(&ms, curr, p_ptr)) {
				// Match found
				double start_idx = static_cast<double>(curr - s_ptr + 1);
				double end_idx = static_cast<double>(res - s_ptr);

				out.assign({start_idx, end_idx});

				if (ms.level > 0) {
					get_captures(ms, out); // Appends to out
				}
				return;
			}
			if (anchor) break;
		}
		while (curr++ < s_end);
	}
	out.clear();
}

// string.format
void string_format(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string format_str = get_string(args[0]);
	std::string result = "";
	int arg_idx = 0;
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

		LuaValue val = args[++arg_idx];
		char buffer[4096];

		switch (spec) {
		case 'c':
			snprintf(buffer, sizeof(buffer), fmt_spec.c_str(), static_cast<int>(get_double(val)));
			break;
		case 'd':
		case 'i':
			snprintf(buffer, sizeof(buffer), fmt_spec.c_str(), static_cast<long long>(get_double(val)));
			break;
		case 'o':
		case 'u':
		case 'x':
		case 'X':
			snprintf(buffer, sizeof(buffer), fmt_spec.c_str(), static_cast<unsigned long long>(get_double(val)));
			break;
		case 'e':
		case 'E':
		case 'f':
		case 'g':
		case 'G':
		case 'a':
		case 'A':
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
					char esc[5];
					snprintf(esc, 5, "\\%03d", static_cast<unsigned char>(c));
					result += esc;
				}
				else {
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
	out.assign({result});
}

// string.gmatch
void string_gmatch(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string s = get_string(args[0]);
	std::string pattern = get_string(args[1]);

	auto s_ptr = std::make_shared<std::string>(s);
	auto p_ptr = std::make_shared<std::string>(pattern);
	auto pos_ptr = std::make_shared<size_t>(0); // Current search index

	// The iterator lambda must return void and take an output buffer
	auto func = [s_ptr, p_ptr, pos_ptr](const LuaValue* _, size_t __, std::vector<LuaValue>& iter_out) -> void {
		const char* s_raw = s_ptr->c_str();
		size_t len = s_ptr->length();
		const char* s_end = s_raw + len;
		const char* p_raw = p_ptr->c_str();
		const char* p_end = p_raw + p_ptr->length();

		if (*pos_ptr > len) {
			// Finished
			return;
		}

		bool anchor = (*p_raw == '^');
		const char* p_eff = anchor ? p_raw + 1 : p_raw;

		const char* curr = s_raw + *pos_ptr;

		while (curr <= s_end) {
			LuaPattern::MatchState ms;
			ms.src_init = s_raw;
			ms.src_end = s_end;
			ms.p_end = p_end;
			ms.level = 0;

			if (const char* res = LuaPattern::match(&ms, curr, p_eff)) {
				size_t match_len = res - curr;
				size_t current_idx = (curr - s_raw);

				if (match_len == 0) {
					if (current_idx < len) {
						*pos_ptr = current_idx + 1;
					}
					else {
						*pos_ptr = len + 1;
					}
				}
				else {
					*pos_ptr = (res - s_raw);
				}

				if (anchor) {
					*pos_ptr = len + 1;
				}

				if (ms.level > 0) {
					iter_out.clear();
					get_captures(ms, iter_out);
				}
				else {
					iter_out.assign({std::string(curr, match_len)});
				}
				return;
			}

			if (anchor) break;
			if (curr == s_end) break;
			curr++;
		}
		*pos_ptr = len + 1;
		// Returns empty (nil)
	};

	out.assign({std::make_shared<LuaFunctionWrapper>(func), std::monostate{}, std::monostate{}});
}

// string.gsub
void string_gsub(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string s = get_string(args[0]);
	std::string pattern = get_string(args[1]);
	LuaValue repl_val = args[2];
	double n_d = n_args >= 4 ? get_double(args[3]) : -1.0;
	long long max_subs = (n_d < 0) ? static_cast<long long>(s.length() + 1) : static_cast<long long>(n_d);

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

	// Reusable buffer for callbacks
	std::vector<LuaValue> callback_buffer;

	while (curr <= s_end && matches < max_subs) {
		LuaPattern::MatchState ms;
		ms.src_init = s_raw;
		ms.src_end = s_end;
		ms.p_end = p_end;
		ms.level = 0;

		if (const char* res = LuaPattern::match(&ms, curr, p_eff)) {
			// Append non-matched part
			result.append(last_match_end, curr - last_match_end);

			// Prepare captures
			std::vector<LuaValue> caps;
			if (ms.level == 0) {
				caps.push_back(std::string(curr, res - curr)); // Cap 0 is whole match
			}
			else {
				caps.push_back(std::string(curr, res - curr));
				get_captures(ms, caps); // Appends cap 1..N
			}

			// Determine Replacement
			std::string replacement_str;
			bool has_rep = false;

			if (std::holds_alternative<std::string>(repl_val) || std::holds_alternative<double>(repl_val)) {
				std::string repl_tmpl = to_cpp_string(repl_val);
				for (size_t i = 0; i < repl_tmpl.length(); ++i) {
					if (repl_tmpl[i] == '%') {
						i++;
						if (i >= repl_tmpl.length()) {
							replacement_str += '%';
							break;
						}
						char c = repl_tmpl[i];
						if (c == '%') replacement_str += '%';
						else if (isdigit(c)) {
							int idx = c - '0';
							if (idx < static_cast<int>(caps.size())) replacement_str += to_cpp_string(caps[idx]);
							else if (idx == 0) replacement_str += to_cpp_string(caps[0]);
						}
						else {
							replacement_str += '%';
							replacement_str += c;
						}
					}
					else {
						replacement_str += repl_tmpl[i];
					}
				}
				has_rep = true;
			}
			else if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(repl_val)) {
				auto func = std::get<std::shared_ptr<LuaFunctionWrapper>>(repl_val);
				callback_buffer.clear();
				if (caps.size() > 1) {
					// Pass [cap1, cap2, ...] skipping index 0 (whole match)
					std::vector<LuaValue> cap_only_args(caps.begin() + 1, caps.end());
					func->func(cap_only_args.data(), cap_only_args.size(), callback_buffer);
				}
				else {
					LuaValue c0 = caps[0];
					func->func(&c0, 1, callback_buffer);
				}

				if (!callback_buffer.empty() && !std::holds_alternative<std::monostate>(callback_buffer[0])) {
					replacement_str = to_cpp_string(callback_buffer[0]);
					has_rep = true;
				}
			}
			else if (std::holds_alternative<std::shared_ptr<LuaObject>>(repl_val)) {
				auto tbl = std::get<std::shared_ptr<LuaObject>>(repl_val);
				std::string key = (caps.size() > 1) ? to_cpp_string(caps[1]) : to_cpp_string(caps[0]);
				LuaValue val = tbl->get(key);
				if (!std::holds_alternative<std::monostate>(val)) {
					replacement_str = to_cpp_string(val);
					has_rep = true;
				}
			}

			if (!has_rep) {
				replacement_str = std::string(curr, res - curr);
			}

			result += replacement_str;
			matches++;
			last_match_end = res;

			if (res == curr) {
				if (curr < s_end) {
					last_match_end++;
					curr++;
				}
				else {
					break;
				}
			}
			else {
				curr = res;
			}
			if (anchor) break;
		}
		else {
			if (curr < s_end) curr++;
			else break;
		}
	}
	result.append(last_match_end, s_end - last_match_end);
	out.clear();
	out.push_back(result);
	out.push_back(static_cast<double>(matches));
}

// string.len
void string_len(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string s = get_string(args[0]);
	out.assign({static_cast<double>(s.length())});
}

// string.lower
void string_lower(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string s = get_string(args[0]);
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	out.assign({s});
}

// string.match
void string_match(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string s = get_string(args[0]);
	std::string pattern = get_string(args[1]);
	double init_d = (n_args >= 3) ? get_double(args[2]) : 1.0;

	long long init = static_cast<long long>(init_d);
	if (init < 0) init = s.length() + init + 1;
	if (init < 1) init = 1;

	if (init > static_cast<long long>(s.length()) + 1) {
		out.assign({std::monostate{}});
		return;
	}

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

		if (const char* res = LuaPattern::match(&ms, curr, p_eff)) {
			if (ms.level > 0) {
				out.clear();
				get_captures(ms, out);
			}
			else {
				out.assign({std::string(curr, res - curr)});
			}
			return;
		}
		if (anchor) break;
	}
	while (curr++ < s_end);
}

// string.pack (Stub)
void string_pack(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) { out.clear(); }
// string.packsize (Stub)
void string_packsize(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) { out.clear(); }

// string.rep
void string_rep(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string s = get_string(args[0]);
	long long n = static_cast<long long>(get_double(args[1]));
	std::string sep = n_args >= 3 && std::holds_alternative<std::string>(args[2]) ? std::get<std::string>(args[2]) : "";

	if (n <= 0) {
		out.assign({""});
		return;
	}

	std::string res;
	res.reserve((s.length() + sep.length()) * static_cast<size_t>(n));

	for (long long i = 0; i < n; ++i) {
		if (i > 0) res += sep;
		res += s;
	}
	out.assign({res});
}

// string.reverse
void string_reverse(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string s = get_string(args[0]);
	std::reverse(s.begin(), s.end());
	out.assign({s});
}

// string.sub
void string_sub(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string s = get_string(args[0]);
	double i_d = n_args >= 2 ? get_double(args[1]) : 1.0;
	double j_d = n_args >= 3 ? get_double(args[2]) : -1.0;

	long long len = s.length();
	long long i = static_cast<long long>(i_d);
	long long j = static_cast<long long>(j_d);

	if (i < 0) i = len + i + 1;
	if (j < 0) j = len + j + 1;
	if (i < 1) i = 1;
	if (j > len) j = len;

	if (i <= j) {
		out.assign({s.substr(i - 1, j - i + 1)});
	}
	else {
		out.assign({""});
	}
}

// string.unpack (Stub)
void string_unpack(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) { out.clear(); }

// string.upper
void string_upper(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string s = get_string(args[0]);
	std::transform(s.begin(), s.end(), s.begin(), ::toupper);
	out.assign({s});
}

// --- C++ Helper Implementations (Updated to use native matcher) ---

void lua_string_match(const LuaValue& str, const LuaValue& pattern, std::vector<LuaValue>& out) {
	LuaValue args[] = {str, pattern};
	string_match(args, 2, out);
}

void lua_string_find(const LuaValue& str, const LuaValue& pattern, std::vector<LuaValue>& out) {
	LuaValue args[] = {str, pattern};
	string_find(args, 2, out);
}

void lua_string_gsub(const LuaValue& str, const LuaValue& pattern, const LuaValue& replacement,
                     std::vector<LuaValue>& out) {
	LuaValue args[] = {str, pattern, replacement};
	string_gsub(args, 3, out);
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

	return lib;
}
