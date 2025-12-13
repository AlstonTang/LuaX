#ifndef STRING_HPP
#define STRING_HPP

#include "lua_object.hpp"
#include "lua_value.hpp"
#include <memory>

// Creates the 'string' library table
std::shared_ptr<LuaObject> create_string_library();

// String library functions - exposed for direct use
// String library functions - exposed for direct use
std::vector<LuaValue> string_byte(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_char(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_dump(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_find(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_format(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_gmatch(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_gsub(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_len(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_lower(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_match(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_pack(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_packsize(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_rep(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_reverse(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_sub(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_unpack(const LuaValue* args, size_t n_args);
std::vector<LuaValue> string_upper(const LuaValue* args, size_t n_args);

// Convenience wrappers used by transpiled code
std::vector<LuaValue> lua_string_match(const LuaValue& str, const LuaValue& pattern);
std::vector<LuaValue> lua_string_find(const LuaValue& str, const LuaValue& pattern);
std::vector<LuaValue> lua_string_gsub(const LuaValue& str, const LuaValue& pattern, const LuaValue& replacement);

#endif // STRING_HPP