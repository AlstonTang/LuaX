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
		std::string_view src;
		const char* src_init;
		const char* p_end;
		int level;

		struct {
			const char* init;
			ptrdiff_t len;
		} capture[LUA_MAXCAPTURES];
	};

	static bool check_class(int c, int cl) {
		bool res;
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
				if (match_class(c, *++p)) return sig;
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
		if (s >= ms->src.data() + ms->src.size()) return false;
		int c = static_cast<unsigned char>(*s);
		switch (*p) {
		case '.': return true;
		case '%': return match_class(c, *(p + 1));
		case '[': return match_bracket_class(c, p, ep - 1);
		default: return (*p == c);
		}
	}

	static const char* match(MatchState* ms, const char* s, const char* p);

	static const char* max_expand(MatchState* ms, const char* s, const char* p, const char* ep) {
		ptrdiff_t i = 0;
		const char* s_end = ms->src.data() + ms->src.size();
		while (s + i < s_end && singlematch(ms, s + i, p, ep)) i++;
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
		int level = ms->level++;
		ms->capture[level].init = s;
		ms->capture[level].len = what;
		const char* res = match(ms, s, p);
		if (!res) ms->level--;
		return res;
	}

	static const char* end_capture(MatchState* ms, const char* s, const char* p) {
		int l = ms->level - 1;
		while (l >= 0 && ms->capture[l].len != CAP_UNFINISHED) --l;
		if (l < 0) throw std::runtime_error("invalid pattern capture");
		ms->capture[l].len = s - ms->capture[l].init;
		const char* res = match(ms, s, p);
		if (!res) ms->capture[l].len = CAP_UNFINISHED;
		return res;
	}

	static const char* classend(const MatchState* ms, const char* p) {
		switch (*p++) {
		case '%':
			if (p == ms->p_end) throw std::runtime_error("malformed pattern");
			return p + 1;
		case '[':
			if (*p == '^') p++;
			do {
				if (p == ms->p_end) throw std::runtime_error("malformed pattern");
				if (*p++ == '%' && p < ms->p_end) p++;
			}
			while (*p != ']');
			return p + 1;
		default: return p;
		}
	}

	static const char* match(MatchState* ms, const char* s, const char* p) {
		if (p == ms->p_end) return s;
		const char* s_end = ms->src.data() + ms->src.size();

		switch (*p) {
		case '(': return (*(p + 1) == ')')
			                 ? start_capture(ms, s, p + 2, CAP_POSITION)
			                 : start_capture(ms, s, p + 1, CAP_UNFINISHED);
		case ')': return end_capture(ms, s, p + 1);
		case '$': if ((p + 1) == ms->p_end) return (s == s_end) ? s : nullptr;
			break;
		case '%':
			if (*(p + 1) >= '0' && *(p + 1) <= '9') {
				int l = *(p + 1) - '1';
				if (l < 0 || l >= ms->level || ms->capture[l].len == CAP_UNFINISHED) throw std::runtime_error(
					"invalid capture index");
				size_t len = ms->capture[l].len;
				if (static_cast<size_t>(s_end - s) >= len && memcmp(ms->capture[l].init, s, len) == 0) return match(
					ms, s + len, p + 2);
				return nullptr;
			}
			break;
		}

		const char* ep = classend(ms, p);
		bool m = (s < s_end) && singlematch(ms, s, p, ep);

		switch (*ep) {
		case '?': {
			const char* res;
			if (m && (res = match(ms, s + 1, ep + 1))) return res;
			return match(ms, s, ep + 1);
		}
		case '*': return max_expand(ms, s, p, ep);
		case '+': return m ? max_expand(ms, s + 1, p, ep) : nullptr;
		case '-': return min_expand(ms, s, p, ep);
		default: return m ? match(ms, s + 1, ep) : nullptr;
		}
	}
}

// --- Helper Functions ---

inline std::string_view get_sv(const LuaValue& v) {
	if (const auto* s = std::get_if<std::string>(&v)) return *s;
	if (const auto* sv = std::get_if<std::string_view>(&v)) return *sv;
	return "";
}

std::string fast_get_string(const LuaValue& v) {
	return std::visit([](auto&& arg) -> std::string {
		using T = std::decay_t<decltype(arg)>;
		if constexpr (std::is_same_v<T, std::string>) return arg;
		if constexpr (std::is_same_v<T, std::string_view>) return std::string(arg);
		if constexpr (std::is_same_v<T, double> || std::is_same_v<T, long long>) {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), arg);
			return std::string(buf, ptr - buf);
		}
		return "";
	}, v);
}

std::string get_string(const LuaValue& v) {
	return std::visit([](auto&& arg) -> std::string {
		using T = std::decay_t<decltype(arg)>;

		if constexpr (std::is_same_v<T, std::string>) {
			return arg; // Copy the existing string
		}
		else if constexpr (std::is_same_v<T, std::string_view>) {
			return std::string(arg);
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

// No longer needed, using LuaObject::get_single_char instead

// Helper to extract captures directly into the output vector
void get_captures(const LuaPattern::MatchState& ms, LuaValueVector& out) {
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
void string_byte(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args == 0) return;
	long long i = (n_args >= 2) ? static_cast<long long>(get_double(args[1])) : 1;
	long long j = (n_args >= 3) ? static_cast<long long>(get_double(args[2])) : i;

    lua_string_byte(args[0], i, j, out);
}


// string.char
void string_char(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	std::string result;
	result.reserve(n_args);
	for (size_t i = 0; i < n_args; ++i) {
		result.push_back(static_cast<char>(static_cast<unsigned char>(get_double(args[i]))));
	}
	out.assign({std::move(result)});
}

// string.dump
void string_dump(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	// Not supported in this interpreter
	out.clear();
}

// string.find
void string_find(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 2) return;
	std::string_view s = get_sv(args[0]);
	std::string_view p = get_sv(args[1]);
	long long init = (n_args >= 3) ? static_cast<long long>(get_double(args[2])) : 1;
	bool plain = (n_args >= 4)
		             ? std::visit([](auto&& arg) {
			             if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, bool>) return arg;
			             return false;
		             }, args[3])
		             : false;

	long long len = s.length();
	if (init < 0) init += len + 1;
	init = std::max(1LL, init);
	out.clear();

	if (init > len + 1) {
		out.push_back(std::monostate{});
		return;
	}

	if (plain) {
		auto pos = s.find(p, static_cast<size_t>(init - 1));
		if (pos != std::string_view::npos) {
			out.push_back(static_cast<double>(pos + 1));
			out.push_back(static_cast<double>(pos + p.length()));
			return;
		}
	}
	else {
		LuaPattern::MatchState ms;
		ms.src = s;
		ms.src_init = s.data();
		ms.p_end = p.data() + p.length();

		const char* s_ptr = s.data() + init - 1;
		const char* s_end = s.data() + len;
		bool anchor = !p.empty() && p[0] == '^';
		const char* p_eff = anchor ? p.data() + 1 : p.data();

		do {
			ms.level = 0;
			if (const char* res = LuaPattern::match(&ms, s_ptr, p_eff)) {
				out.push_back(static_cast<double>(s_ptr - s.data() + 1));
				out.push_back(static_cast<double>(res - s.data()));
				for (int i = 0; i < ms.level; ++i) {
					if (ms.capture[i].len == LuaPattern::CAP_POSITION)
						out.push_back(static_cast<double>(ms.capture[i].init - ms.src_init + 1));
					else
						out.push_back(std::string(ms.capture[i].init, ms.capture[i].len));
				}
				return;
			}
		}
		while (s_ptr++ < s_end && !anchor);
	}
	out.push_back(std::monostate{});
}

// string.format
void string_format(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	std::string_view fmt = get_sv(args[0]);
	std::string result;
	result.reserve(fmt.size() + 32);

	size_t arg_idx = 1;
	for (size_t i = 0; i < fmt.size(); ++i) {
		if (fmt[i] == '%' && i + 1 < fmt.size()) {
			if (fmt[++i] == '%') {
				result += '%';
			}
			else {
				// Simplified logic: collect flags/width
				size_t start = i - 1;
				while (i < fmt.size() && (strchr("-+ #0.", fmt[i]) || isdigit(fmt[i]))) i++;
				char spec = fmt[i];
				std::string sub_fmt(fmt.data() + start, i - start + 1);

				char buf[512]; // Large enough for most numbers
				int written = 0;
				const auto& val = args[arg_idx++];

				if (spec == 's') {
					std::string s_val = fast_get_string(val);
					// Use snprintf only if width/flags are present, else append directly
					if (sub_fmt == "%s") result.append(s_val);
					else {
						std::string dyn_fmt = sub_fmt;
						result.resize(result.size() + s_val.size() + 128); // Over-allocate
						written = snprintf(&result[result.size() - (s_val.size() + 128)], 128 + s_val.size(),
						                   dyn_fmt.c_str(), s_val.c_str());
						result.resize(result.size() - (s_val.size() + 128) + written);
					}
					continue;
				}

				// Generic numeric formatting
				if (strchr("diouxX", spec)) written = snprintf(buf, sizeof(buf), sub_fmt.c_str(),
				                                               (long long)get_double(val));
				else if (strchr("eEfgGaA", spec)) written =
					snprintf(buf, sizeof(buf), sub_fmt.c_str(), get_double(val));
				else if (spec == 'c') written = snprintf(buf, sizeof(buf), sub_fmt.c_str(), (int)get_double(val));

				if (written > 0) result.append(buf, written);
			}
		}
		else {
			result += fmt[i];
		}
	}
	out.assign({std::move(result)});
}

// string.gmatch
void string_gmatch(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 2) return;
	// We must copy the strings into shared_ptrs because the iterator might outlive the call
	auto s_ptr = std::make_shared<std::string>(get_sv(args[0]));
	auto p_ptr = std::make_shared<std::string>(get_sv(args[1]));
	auto last_match_ptr = std::make_shared<size_t>(0);
	auto done_ptr = std::make_shared<bool>(false);

	auto iter = [s_ptr, p_ptr, last_match_ptr, done_ptr](const LuaValue*, size_t, LuaValueVector& iter_out) {
		if (*done_ptr) return;

		std::string_view s(*s_ptr);
		std::string_view p(*p_ptr);

		LuaPattern::MatchState ms;
		ms.src = s;
		ms.src_init = s.data();
		ms.p_end = p.data() + p.length();

		const char* s_start = s.data();
		const char* s_end = s_start + s.length();
		const char* curr = s_start + *last_match_ptr;

		bool anchor = !p.empty() && p[0] == '^';
		const char* p_eff = anchor ? p.data() + 1 : p.data();

		while (curr <= s_end) {
			ms.level = 0;
			if (const char* res = LuaPattern::match(&ms, curr, p_eff)) {
				// Update match position for next iteration
				size_t next_pos = res - s_start;
				if (res == curr) next_pos++; // Advance 1 if empty match
				*last_match_ptr = next_pos;
				if (anchor) *done_ptr = true;

				iter_out.clear();
				if (ms.level == 0) {
					iter_out.push_back(std::string(curr, res - curr));
				}
				else {
					for (int i = 0; i < ms.level; ++i) {
						if (ms.capture[i].len == LuaPattern::CAP_POSITION)
							iter_out.push_back(static_cast<double>(ms.capture[i].init - ms.src_init + 1));
						else
							iter_out.push_back(std::string(ms.capture[i].init, ms.capture[i].len));
					}
				}
				return;
			}
			if (anchor) break;
			curr++;
		}
		*done_ptr = true;
	};

	out.assign({std::make_shared<LuaFunctionWrapper>(iter), std::monostate{}, std::monostate{}});
}

// string.gsub
void string_gsub(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 3) return;
	std::string_view s = get_sv(args[0]);
	std::string_view p = get_sv(args[1]);
	const auto& repl = args[2];
	long long max_s = (n_args >= 4) ? static_cast<long long>(get_double(args[3])) : -1;

	std::string result;
	result.reserve(s.size()); // Pre-reserve to minimize reallocs

	LuaPattern::MatchState ms;
	ms.src = s;
	ms.src_init = s.data();
	ms.p_end = p.data() + p.length();

	const char* s_start = s.data();
	const char* s_end = s_start + s.length();
	const char* curr = s_start;
	const char* last_match_end = s_start;

	bool anchor = !p.empty() && p[0] == '^';
	const char* p_eff = anchor ? p.data() + 1 : p.data();
	long long count = 0;

	LuaValueVector callback_args; // Reused for performance

	while (curr <= s_end && (max_s < 0 || count < max_s)) {
		ms.level = 0;
		if (const char* res = LuaPattern::match(&ms, curr, p_eff)) {
			count++;
			// Append non-matched part
			result.append(last_match_end, curr - last_match_end);

			// Handle replacement logic
			std::string_view r_text;
			bool is_string_repl = false;
			if (const auto* s = std::get_if<std::string>(&repl)) {
				r_text = *s;
				is_string_repl = true;
			} else if (const auto* sv = std::get_if<std::string_view>(&repl)) {
				r_text = *sv;
				is_string_repl = true;
			}

			if (is_string_repl) {
				// String replacement with % captures
				for (size_t i = 0; i < r_text.length(); ++i) {
					if (r_text[i] == '%' && i + 1 < r_text.length()) {
						char next = r_text[++i];
						if (isdigit(next)) {
							int cap_idx = next - '0';
							if (cap_idx == 0) result.append(curr, res - curr);
							else if (cap_idx <= ms.level) result.append(ms.capture[cap_idx - 1].init,
							                                            ms.capture[cap_idx - 1].len);
						}
						else if (next == '%') {
							result += '%';
						}
						else {
							result += '%';
							result += next;
						}
					}
					else {
						result += r_text[i];
					}
				}
			}
			else if (auto* r_func = std::get_if<std::shared_ptr<LuaFunctionWrapper>>(&repl)) {
				// Function replacement
				callback_args.clear();
				if (ms.level == 0) callback_args.push_back(std::string(curr, res - curr));
				else {
					for (int i = 0; i < ms.level; ++i)
						callback_args.push_back(std::string(ms.capture[i].init, ms.capture[i].len));
				}
				LuaValueVector cb_res;
				(*r_func)->func(callback_args.data(), callback_args.size(), cb_res);
				if (!cb_res.empty() && !std::holds_alternative<std::monostate>(cb_res[0]))
					result.append(fast_get_string(cb_res[0]));
				else
					result.append(curr, res - curr);
			}
			else if (auto* r_obj = std::get_if<std::shared_ptr<LuaObject>>(&repl)) {
				// Table replacement
				std::string key = (ms.level == 0)
					                  ? std::string(curr, res - curr)
					                  : std::string(ms.capture[0].init, ms.capture[0].len);
				auto val = (*r_obj)->get(key);
				if (!std::holds_alternative<std::monostate>(val))
					result.append(fast_get_string(val));
				else
					result.append(curr, res - curr);
			}

			last_match_end = res;
			curr = res + (res == curr ? 1 : 0); // Avoid infinite loop on empty match
			if (anchor) break;
			continue;
		}
		if (anchor) break;
		curr++;
	}

	result.append(last_match_end, s_end - last_match_end);
	out.assign({std::move(result), static_cast<double>(count)});
}

// string.len
void string_len(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	std::string s = get_string(args[0]);
	out.assign({static_cast<double>(s.length())});
}

// string.lower
void string_lower(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	std::string s = get_string(args[0]);
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	out.assign({s});
}

// string.match

void string_match(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	std::string_view s = get_sv(args[0]);
	std::string_view p = get_sv(args[1]);
	long long init = (n_args >= 3) ? static_cast<long long>(get_double(args[2])) : 1;
	if (init < 0) init += s.length() + 1;
	init = std::max(1LL, init);

	out.clear();
	if (init > (long long)s.length() + 1) {
		out.push_back(std::monostate{});
		return;
	}

	LuaPattern::MatchState ms;
	ms.src = s;
	ms.src_init = s.data();
	ms.p_end = p.data() + p.length();

	const char* s_ptr = s.data() + init - 1;
	const char* s_end = s.data() + s.length();
	bool anchor = !p.empty() && p[0] == '^';
	const char* p_eff = anchor ? p.data() + 1 : p.data();

	do {
		ms.level = 0;
		if (const char* res = LuaPattern::match(&ms, s_ptr, p_eff)) {
			if (ms.level == 0) out.push_back(std::string(s_ptr, res - s_ptr));
			else {
				for (int i = 0; i < ms.level; ++i) {
					if (ms.capture[i].len == LuaPattern::CAP_POSITION)
						out.push_back((double)(ms.capture[i].init - ms.src_init + 1));
					else
						out.push_back(std::string(ms.capture[i].init, ms.capture[i].len));
				}
			}
			return;
		}
	}
	while (s_ptr++ < s_end && !anchor);

	out.push_back(std::monostate{});
}

// string.pack (Stub)
void string_pack(const LuaValue* args, size_t n_args, LuaValueVector& out) { out.clear(); }
// string.packsize (Stub)
void string_packsize(const LuaValue* args, size_t n_args, LuaValueVector& out) { out.clear(); }

// string.rep
void string_rep(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	std::string_view s = get_sv(args[0]);
	long long n = (n_args >= 2) ? static_cast<long long>(get_double(args[1])) : 0;
	std::string_view sep = (n_args >= 3) ? get_sv(args[2]) : "";

	if (n <= 0) {
		out.assign({LuaValue(std::string_view(""))});
		return;
	}
	if (n == 1) {
		out.assign({std::string(s)});
		return;
	}

	std::string res;
	size_t total_size = (s.length() * n) + (sep.length() * (n - 1));
	res.reserve(total_size);

	for (long long i = 0; i < n; ++i) {
		if (i > 0 && !sep.empty()) res.append(sep);
		res.append(s);
	}
	out.assign({std::move(res)});
}

// string.reverse
void string_reverse(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	std::string s = get_string(args[0]);
	std::reverse(s.begin(), s.end());
	out.assign({s});
}

void string_sub(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 2) return;
	long long i = (n_args >= 2) ? static_cast<long long>(get_double(args[1])) : 1;
	long long j = (n_args >= 3) ? static_cast<long long>(get_double(args[2])) : -1;

    out.assign({lua_string_sub(args[0], i, j)});
}

// string.unpack (Stub)
void string_unpack(const LuaValue* args, size_t n_args, LuaValueVector& out) { out.clear(); }

// string.upper
void string_upper(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	std::string s = get_string(args[0]);
	std::transform(s.begin(), s.end(), s.begin(), ::toupper);
	out.assign({s});
}

// --- C++ Helper Implementations (Updated to use native matcher) ---

void lua_string_match(const LuaValue& str, const LuaValue& pattern, LuaValueVector& out) {
	LuaValue args[] = {str, pattern};
	string_match(args, 2, out);
}

void lua_string_find(const LuaValue& str, const LuaValue& pattern, LuaValueVector& out) {
	LuaValue args[] = {str, pattern};
	string_find(args, 2, out);
}

void lua_string_gsub(const LuaValue& str, const LuaValue& pattern, const LuaValue& replacement,
                     LuaValueVector& out) {
	LuaValue args[] = {str, pattern, replacement};
	string_gsub(args, 3, out);
}

void lua_string_byte(const LuaValue& str, long long i, long long j, LuaValueVector& out) {
    std::string_view s = get_sv(str);
    long long len = static_cast<long long>(s.length());
    if (i < 0) i += len + 1;
    if (j < 0) j += len + 1;
    i = std::max(1LL, i);
    j = std::min(len, j);

    out.clear();
    if (i > j) return;

    size_t count = static_cast<size_t>(j - i + 1);
    out.resize(count);
    for (size_t k = 0; k < count; ++k) {
        out[k] = static_cast<double>(static_cast<unsigned char>(s[i + k - 1]));
    }
}

LuaValue lua_string_sub(const LuaValue& str, long long i, long long j) {
    std::string_view s = get_sv(str);
    long long len = static_cast<long long>(s.length());
    if (i < 0) i += len + 1;
    if (j < 0) j += len + 1;
    i = std::max(1LL, i);
    j = std::min(len, j);

    if (i > j) return std::string("");
    return std::string(s.substr(i - 1, j - i + 1));
}

// --- Library Creation ---

std::shared_ptr<LuaObject> create_string_library() {
	static std::shared_ptr<LuaObject> lib;
	if (lib) return lib;

	lib = LuaObject::create({
		{LuaValue(std::string_view("byte")), std::make_shared<LuaFunctionWrapper>(string_byte)},
		{LuaValue(std::string_view("char")), std::make_shared<LuaFunctionWrapper>(string_char)},
		{LuaValue(std::string_view("dump")), std::make_shared<LuaFunctionWrapper>(string_dump)},
		{LuaValue(std::string_view("find")), std::make_shared<LuaFunctionWrapper>(string_find)},
		{LuaValue(std::string_view("format")), std::make_shared<LuaFunctionWrapper>(string_format)},
		{LuaValue(std::string_view("gmatch")), std::make_shared<LuaFunctionWrapper>(string_gmatch)},
		{LuaValue(std::string_view("gsub")), std::make_shared<LuaFunctionWrapper>(string_gsub)},
		{LuaValue(std::string_view("len")), std::make_shared<LuaFunctionWrapper>(string_len)},
		{LuaValue(std::string_view("lower")), std::make_shared<LuaFunctionWrapper>(string_lower)},
		{LuaValue(std::string_view("match")), std::make_shared<LuaFunctionWrapper>(string_match)},
		{LuaValue(std::string_view("pack")), std::make_shared<LuaFunctionWrapper>(string_pack)},
		{LuaValue(std::string_view("packsize")), std::make_shared<LuaFunctionWrapper>(string_packsize)},
		{LuaValue(std::string_view("rep")), std::make_shared<LuaFunctionWrapper>(string_rep)},
		{LuaValue(std::string_view("reverse")), std::make_shared<LuaFunctionWrapper>(string_reverse)},
		{LuaValue(std::string_view("sub")), std::make_shared<LuaFunctionWrapper>(string_sub)},
		{LuaValue(std::string_view("unpack")), std::make_shared<LuaFunctionWrapper>(string_unpack)},
		{LuaValue(std::string_view("upper")), std::make_shared<LuaFunctionWrapper>(string_upper)}
	});

	return lib;
}
