#include "utf8.hpp"
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <cmath>

// Helper function to encode a single Unicode codepoint to UTF-8
std::string encode_utf8(int codepoint) {
	std::string result;
	if (codepoint < 0x80) {
		result += static_cast<char>(codepoint);
	}
	else if (codepoint < 0x800) {
		result += static_cast<char>(0xC0 | (codepoint >> 6));
		result += static_cast<char>(0x80 | (codepoint & 0x3F));
	}
	else if (codepoint < 0x10000) {
		result += static_cast<char>(0xE0 | (codepoint >> 12));
		result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
		result += static_cast<char>(0x80 | (codepoint & 0x3F));
	}
	else if (codepoint < 0x110000) {
		result += static_cast<char>(0xF0 | (codepoint >> 18));
		result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
		result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
		result += static_cast<char>(0x80 | (codepoint & 0x3F));
	}
	else {
		throw std::runtime_error("Invalid Unicode codepoint");
	}
	return result;
}

// Helper function to get the length of a UTF-8 character from its leading byte
int get_utf8_char_length(unsigned char byte) {
	if ((byte & 0x80) == 0x00) return 1; // 0xxxxxxx
	if ((byte & 0xE0) == 0xC0) return 2; // 110xxxxx
	if ((byte & 0xF0) == 0xE0) return 3; // 1110xxxx
	if ((byte & 0xF8) == 0xF0) return 4; // 11110xxx
	return 0; // Invalid UTF-8 start byte
}

// Helper function to decode a single UTF-8 character from a string
int decode_utf8(const std::string& s, size_t& offset) {
	if (offset >= s.length()) {
		return -1; // No more characters
	}

	unsigned char byte1 = static_cast<unsigned char>(s[offset]);
	const int len = get_utf8_char_length(byte1);

	if (len == 0 || offset + len > s.length()) {
		// Invalid UTF-8 sequence or incomplete character
		// For robustness, we might want to advance by 1 byte and return a replacement char
		offset++;
		return 0xFFFD; // Unicode replacement character
	}

	int codepoint = 0;
	if (len == 1) {
		codepoint = byte1;
	}
	else if (len == 2) {
		codepoint = (byte1 & 0x1F) << 6;
		codepoint |= (static_cast<unsigned char>(s[offset + 1]) & 0x3F);
	}
	else if (len == 3) {
		codepoint = (byte1 & 0x0F) << 12;
		codepoint |= (static_cast<unsigned char>(s[offset + 1]) & 0x3F) << 6;
		codepoint |= (static_cast<unsigned char>(s[offset + 2]) & 0x3F);
	}
	else {
		codepoint = (byte1 & 0x07) << 18;
		codepoint |= (static_cast<unsigned char>(s[offset + 1]) & 0x3F) << 12;
		codepoint |= (static_cast<unsigned char>(s[offset + 2]) & 0x3F) << 6;
		codepoint |= (static_cast<unsigned char>(s[offset + 3]) & 0x3F);
	}
	offset += len;
	return codepoint;
}

// utf8.char (integer codepoint(s) to string)
void utf8_char(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	std::string result_str;
	for (size_t i = 0; i < n_args; ++i) {
		LuaValue cp_val = args[i];
		switch (cp_val.index()) {
			case INDEX_NIL:
				goto end_loop; // break the loop
			case INDEX_DOUBLE:
				result_str += encode_utf8(static_cast<int>(cp_val.get<double>()));
				break;
			case INDEX_INTEGER:
				result_str += encode_utf8(static_cast<int>(cp_val.get<long long>()));
				break;
			default:
				throw std::runtime_error("bad argument #" + std::to_string(i + 1) + " to 'char' (number expected)");
		}
	}
end_loop:
	out.assign({result_str});
	return;
}

// utf8.charpattern
void utf8_charpattern(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({std::string("[\0-\x7F\xC2-\xF4][\x80-\xBF]*")});
	return;
}

// utf8.codepoint (string to integer codepoint(s))
void utf8_codepoint(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	LuaValue s_val = args[0];
	std::string s;
	switch (s_val.index()) {
		case INDEX_STRING: s = std::string(s_val.get<std::string_view>()); break;
		case INDEX_STRING_VIEW: s = std::string(s_val.get<std::string_view>()); break;
		default:
			throw std::runtime_error("bad argument #1 to 'codepoint' (string expected)");
	}

	double i_double = 1.0;
	if (n_args >= 2) {
		switch (args[1].index()) {
			case INDEX_DOUBLE: i_double = args[1].get<double>(); break;
			case INDEX_INTEGER: i_double = static_cast<double>(args[1].get<long long>()); break;
			default: break;
		}
	}
	int i = static_cast<int>(i_double);

	double j_double = i_double;
	if (n_args >= 3) {
		switch (args[2].index()) {
			case INDEX_DOUBLE: j_double = args[2].get<double>(); break;
			case INDEX_INTEGER: j_double = static_cast<double>(args[2].get<long long>()); break;
			default: break;
		}
	}
	int j = static_cast<int>(j_double);
	size_t current_byte_offset = 0;

	// Iterate through the string to find the starting byte offset for the i-th character
	size_t start_byte_offset = 0;
	for (int k = 1; k < i; ++k) {
		if (current_byte_offset >= s.length()) {
			out.assign({});
			return; // i is out of bounds
		}
		size_t prev_offset = current_byte_offset;
		decode_utf8(s, current_byte_offset); // Just advance offset
		if (current_byte_offset == prev_offset) { // If decode_utf8 didn't advance, it's an error or end
			{
				out.assign({});
				return;
			};
		}
	}
	start_byte_offset = current_byte_offset;

	// Now extract codepoints from i to j
	current_byte_offset = start_byte_offset;
	out.clear();
	for (int k = i; k <= j; ++k) {
		if (current_byte_offset >= s.length()) {
			break; // Reached end of string
		}
		size_t prev_offset = current_byte_offset;
		int codepoint = decode_utf8(s, current_byte_offset);
		if (current_byte_offset == prev_offset) { // If decode_utf8 didn't advance, it's an error or end
			break;
		}
		out.push_back(LuaValue(static_cast<double>(codepoint)));
	}
}

// utf8.codes (iterator for codepoints)
void utf8_codes_iterator(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	LuaValue s_val = args[0];
	std::string s;
	switch (s_val.index()) {
		case INDEX_STRING: s = std::string(s_val.get<std::string_view>()); break;
		case INDEX_STRING_VIEW: s = std::string(s_val.get<std::string_view>()); break;
		default:
			out.assign({LuaValue()});
			return;
	}

	LuaValue offset_val = args[1];
	size_t offset = 0;
	switch (offset_val.index()) {
		case INDEX_DOUBLE: offset = static_cast<size_t>(offset_val.get<double>()); break;
		case INDEX_INTEGER: offset = static_cast<size_t>(offset_val.get<long long>()); break;
		default: break;
	}

	if (offset >= s.length()) {
		out.assign({LuaValue()});
		return;
	}

	size_t original_offset = offset;
	int codepoint = decode_utf8(s, offset);

	if (offset > original_offset) {
		out.assign({LuaValue(static_cast<double>(offset)), LuaValue(static_cast<double>(codepoint))});
		return;
	}
	else {
		out.assign({LuaValue()});
		return;
	}
}

void utf8_codes(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	LuaValue s_val = args[0];
	if (s_val.index() != INDEX_STRING && s_val.index() != INDEX_STRING_VIEW) {
		throw std::runtime_error("bad argument #1 to 'codes' (string expected)");
	}
	out.assign({LUA_C_FUNC(utf8_codes_iterator), s_val, LuaValue(0.0)});
	return;
}

// utf8.len (length of UTF-8 string)
void utf8_len(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	LuaValue s_val = args[0];
	std::string s;
	switch (s_val.index()) {
		case INDEX_STRING: s = std::string(s_val.get<std::string_view>()); break;
		case INDEX_STRING_VIEW: s = std::string(s_val.get<std::string_view>()); break;
		default:
			throw std::runtime_error("bad argument #1 to 'len' (string expected)");
	}

	size_t len = 0;
	size_t offset = 0;
	while (offset < s.length()) {
		size_t prev_offset = offset;
		decode_utf8(s, offset);
		if (offset == prev_offset) { // Should not happen with valid UTF-8, but for safety
			offset++; // Advance by one byte to avoid infinite loop
		}
		len++;
	}
	out.assign({LuaValue(static_cast<double>(len))});
	return;
}

// utf8.offset (byte offset of n-th character)
void utf8_offset(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	LuaValue s_val = args[0];
	std::string s;
	switch (s_val.index()) {
		case INDEX_STRING: s = std::string(s_val.get<std::string_view>()); break;
		case INDEX_STRING_VIEW: s = std::string(s_val.get<std::string_view>()); break;
		default:
			throw std::runtime_error("bad argument #1 to 'offset' (string expected)");
	}

	LuaValue n_val = (n_args >= 2) ? args[1] : LuaValue();
	int n = 0;
	switch (n_val.index()) {
		case INDEX_DOUBLE: n = static_cast<int>(n_val.get<double>()); break;
		case INDEX_INTEGER: n = static_cast<int>(n_val.get<long long>()); break;
		default:
			throw std::runtime_error("bad argument #2 to 'offset' (number expected)");
	}

	// Optional third argument: pos (starting position in bytes)
	LuaValue pos_val = (n_args >= 3) ? args[2] : LuaValue();
	size_t byte_offset = 0;
	switch (pos_val.index()) {
		case INDEX_DOUBLE: byte_offset = static_cast<size_t>(pos_val.get<double>()) - 1; break;
		case INDEX_INTEGER: byte_offset = static_cast<size_t>(pos_val.get<long long>()) - 1; break;
		default: break;
	}

	if (pos_val.index() != INDEX_NIL) {
		if (byte_offset >= s.length()) {
			out.assign({LuaValue()});
			return; // pos is out of bounds
		}
		// Adjust byte_offset to be the start of a UTF-8 character
		while (byte_offset > 0 && (static_cast<unsigned char>(s[byte_offset]) & 0xC0) == 0x80) {
			byte_offset--;
		}
	}

	if (n == 0) {
		// Find the byte offset of the character *after* the last character
		size_t current_offset = 0;
		while (current_offset < s.length()) {
			size_t prev_offset = current_offset;
			decode_utf8(s, current_offset);
			if (current_offset == prev_offset) { // Should not happen with valid UTF-8
				current_offset++;
			}
		}
		out.assign({LuaValue(static_cast<double>(current_offset + 1))});
		return; // Lua is 1-based
	}

	size_t current_byte_offset = byte_offset;

	if (n > 0) {
		int char_count = 0;
		while (current_byte_offset < s.length()) {
			char_count++;
			if (char_count == n) {
				out.assign({LuaValue(static_cast<double>(current_byte_offset + 1))});
				return; // Lua is 1-based
			}
			size_t prev_offset = current_byte_offset;
			decode_utf8(s, current_byte_offset);
			if (current_byte_offset == prev_offset) { // Should not happen with valid UTF-8
				current_byte_offset++;
			}
		}
	}
	else { // n < 0, count from the end
		std::vector<size_t> char_start_offsets;
		size_t temp_offset = 0;
		while (temp_offset < s.length()) {
			char_start_offsets.push_back(temp_offset);
			size_t prev_offset = temp_offset;
			decode_utf8(s, temp_offset);
			if (temp_offset == prev_offset) { // Should not happen with valid UTF-8
				temp_offset++;
			}
		}

		unsigned long abs_n = std::abs(n);
		if (abs_n <= char_start_offsets.size()) {
			out.assign({LuaValue(static_cast<double>(char_start_offsets[char_start_offsets.size() - abs_n] + 1))});
			return;
		}
	}

	out.assign({LuaValue()});
	return; // nil for out of bounds
}

LuaObject* create_utf8_library() {
	static LuaObject* utf8_lib;
	if (utf8_lib) return utf8_lib;

	utf8_lib = new LuaObject();
	utf8_lib->set("char", LUA_C_FUNC(utf8_char));
	utf8_lib->set("charpattern", std::string("[%z\x01-\x7F\xC2-\xF4][\x80-\xBF]*"));
	utf8_lib->set("codes", LUA_C_FUNC(utf8_codes));
	utf8_lib->set("codepoint", LUA_C_FUNC(utf8_codepoint));
	utf8_lib->set("len", LUA_C_FUNC(utf8_len));
	utf8_lib->set("offset", LUA_C_FUNC(utf8_offset));

	return utf8_lib;
}
