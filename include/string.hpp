#ifndef STRING_HPP
#define STRING_HPP

#include "lua_object.hpp"
#include "lua_value.hpp"
#include <memory>

// Creates the 'string' library table
std::shared_ptr<LuaObject> create_string_library();

// String library functions - exposed for direct use
void string_byte(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_char(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_dump(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_find(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_format(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_gmatch(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_gsub(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_len(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_lower(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_match(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_pack(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_packsize(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_rep(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_reverse(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_sub(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_unpack(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void string_upper(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);

// Convenience wrappers used by transpiled code
void lua_string_match(const LuaValue& str, const LuaValue& pattern, std::vector<LuaValue>& out);
void lua_string_find(const LuaValue& str, const LuaValue& pattern, std::vector<LuaValue>& out);
void lua_string_gsub(const LuaValue& str, const LuaValue& pattern, const LuaValue& replacement,
                     std::vector<LuaValue>& out);

#endif // STRING_HPP
