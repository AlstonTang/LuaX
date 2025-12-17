#include "lua_object.hpp"
#include <iostream>
#include <sstream>
#include <variant>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <charconv>
#include "coroutine.hpp" // Ensure full definition of LuaCoroutine is available

// ==========================================
// Internal Helper Functions
// ==========================================

bool is_integer_key(double d, long long& out) {
	long long l = static_cast<long long>(d);
	if (d == static_cast<double>(l)) {
		out = l;
		return true;
	}
	return false;
}

std::string value_to_key_string(const LuaValue& key) {
	if (std::holds_alternative<std::string>(key)) {
		return std::get<std::string>(key);
	}
	return to_cpp_string(key);
}

// ==========================================
// LuaObject Class Implementation
// ==========================================

LuaValue LuaObject::get(const std::string& key) {
	return get_item(LuaValue(key));
}

void LuaObject::set(const std::string& key, const LuaValue& value) {
	set_item(LuaValue(key), value);
}

void LuaObject::set_metatable(const std::shared_ptr<LuaObject>& mt) {
	metatable = mt;
}

LuaValue LuaObject::get_item(const LuaValue& key) {
	// 1. Array Part (Integer/Double-as-int)
	if (std::holds_alternative<long long>(key)) {
		long long idx = std::get<long long>(key);
		auto it = array_properties.find(idx);
		if (it != array_properties.end()) return it->second;
	} else if (std::holds_alternative<double>(key)) {
		long long idx;
		if (is_integer_key(std::get<double>(key), idx)) {
			auto it = array_properties.find(idx);
			if (it != array_properties.end()) return it->second;
		}
	}

	// 2. Hash Part
	std::string key_str = value_to_key_string(key);
	auto it = properties.find(key_str);
	if (it != properties.end()) {
		return it->second;
	}

	// 3. Metatable __index
	if (metatable) {
		auto index_val = metatable->get_item("__index");
		if (std::holds_alternative<std::shared_ptr<LuaObject>>(index_val)) {
			// Recursive table access
			return std::get<std::shared_ptr<LuaObject>>(index_val)->get_item(key);
		} else if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(index_val)) {
			// Call function, need buffer
			std::vector<LuaValue> results;
			results.reserve(1);
			LuaValue args[] = {shared_from_this(), key};
			std::get<std::shared_ptr<LuaFunctionWrapper>>(index_val)->func(args, 2, results);
			return results.empty() ? LuaValue(std::monostate{}) : results[0];
		}
	}

	return std::monostate{};
}

void LuaObject::set_item(const LuaValue& key, const LuaValue& value) {
	bool key_exists = false;
	long long int_key = 0;
	bool is_int = false;

	// Check if key exists in array part
	if (std::holds_alternative<long long>(key)) {
		int_key = std::get<long long>(key);
		is_int = true;
		key_exists = array_properties.count(int_key);
	} else if (std::holds_alternative<double>(key)) {
		if (is_integer_key(std::get<double>(key), int_key)) {
			is_int = true;
			key_exists = array_properties.count(int_key);
		}
	} 
	
	// Check if key exists in hash part
	std::string str_key;
	if (!is_int) {
		str_key = value_to_key_string(key);
		key_exists = properties.count(str_key);
	}

	// __newindex logic
	if (!key_exists && metatable) {
		auto newindex_val = metatable->get_item("__newindex");
		if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(newindex_val)) {
			std::vector<LuaValue> dummy_out;
			LuaValue args[] = {shared_from_this(), key, value};
			std::get<std::shared_ptr<LuaFunctionWrapper>>(newindex_val)->func(args, 3, dummy_out);
			return;
		} else if (std::holds_alternative<std::shared_ptr<LuaObject>>(newindex_val)) {
			std::get<std::shared_ptr<LuaObject>>(newindex_val)->set_item(key, value);
			return;
		}
	}

	// Raw set
	if (std::holds_alternative<std::monostate>(value)) {
		if (is_int) array_properties.erase(int_key);
		else properties.erase(str_key);
	} else {
		if (is_int) array_properties[int_key] = value;
		else properties[str_key] = value;
	}
}

void LuaObject::set_item(const LuaValue& key, const std::vector<LuaValue>& value) {
	set_item(key, value.empty() ? LuaValue(std::monostate{}) : value[0]);
}

// ==========================================
// Type Conversions & Printing
// ==========================================

std::string to_cpp_string(const LuaValue& value) {
	if (std::holds_alternative<double>(value)) {
		double d = std::get<double>(value);
		long long l;
		if (is_integer_key(d, l)) return std::to_string(l);
		std::stringstream ss;
		ss << std::setprecision(std::numeric_limits<double>::max_digits10) << d;
		return ss.str();
	} else if (std::holds_alternative<long long>(value)) {
		return std::to_string(std::get<long long>(value));
	} else if (std::holds_alternative<std::string>(value)) {
		return std::get<std::string>(value);
	} else if (std::holds_alternative<bool>(value)) {
		return std::get<bool>(value) ? "true" : "false";
	} else if (std::holds_alternative<std::shared_ptr<LuaObject>>(value)) {
		std::stringstream ss;
		ss << "table: " << std::get<std::shared_ptr<LuaObject>>(value).get();
		return ss.str();
	} else if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(value)) {
		return "function"; 
	} else if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(value)) {
		return "thread";
	}
	return "nil";
}

std::string to_cpp_string(const std::vector<LuaValue>& value) {
	return value.empty() ? "nil" : to_cpp_string(value[0]);
}

std::string get_lua_type_name(const LuaValue& val) {
	if (std::holds_alternative<std::monostate>(val)) return "nil";
	if (std::holds_alternative<bool>(val)) return "boolean";
	if (std::holds_alternative<double>(val) || std::holds_alternative<long long>(val)) return "number";
	if (std::holds_alternative<std::string>(val)) return "string";
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) return "table";
	if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(val)) return "function";
	if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(val)) return "thread";
	return "userdata";
}

void print_value(const LuaValue& value) {
	std::cout << to_cpp_string(value);
}

// ==========================================
// Comparison Logic
// ==========================================

bool operator<=(const LuaValue& lhs, const LuaValue& rhs) {
	return lua_less_equals(lhs, rhs);
}

bool lua_less_than(const LuaValue& a, const LuaValue& b) {
	// 1. Number vs Number
	if (std::holds_alternative<double>(a)) {
		if (std::holds_alternative<double>(b)) return std::get<double>(a) < std::get<double>(b);
		if (std::holds_alternative<long long>(b)) return std::get<double>(a) < static_cast<double>(std::get<long long>(b));
	}
	else if (std::holds_alternative<long long>(a)) {
		if (std::holds_alternative<long long>(b)) return std::get<long long>(a) < std::get<long long>(b);
		if (std::holds_alternative<double>(b)) return static_cast<double>(std::get<long long>(a)) < std::get<double>(b);
	}
	// 2. String vs String
	else if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
		return std::get<std::string>(a) < std::get<std::string>(b);
	}
	
	// 3. Metamethod __lt
	// Helper buffer for metamethod calls
	std::vector<LuaValue> res; 
	res.reserve(1);

	if (std::holds_alternative<std::shared_ptr<LuaObject>>(a)) {
		auto t = std::get<std::shared_ptr<LuaObject>>(a);
		if (t->metatable) {
			auto lt = t->metatable->get_item("__lt");
			if (!std::holds_alternative<std::monostate>(lt)) {
				call_lua_value(lt, res, a, b);
				return !res.empty() && is_lua_truthy(res[0]);
			}
		}
	}
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(b)) {
		auto t = std::get<std::shared_ptr<LuaObject>>(b);
		if (t->metatable) {
			 auto lt = t->metatable->get_item("__lt");
			 if (!std::holds_alternative<std::monostate>(lt)) {
				call_lua_value(lt, res, a, b);
				return !res.empty() && is_lua_truthy(res[0]);
			 }
		}
	}

	throw std::runtime_error("attempt to compare " + get_lua_type_name(a) + " with " + get_lua_type_name(b));
}

bool lua_less_equals(const LuaValue& a, const LuaValue& b) {
	// 1. Number vs Number
	if (std::holds_alternative<double>(a)) {
		if (std::holds_alternative<double>(b)) return std::get<double>(a) <= std::get<double>(b);
		if (std::holds_alternative<long long>(b)) return std::get<double>(a) <= static_cast<double>(std::get<long long>(b));
	}
	else if (std::holds_alternative<long long>(a)) {
		if (std::holds_alternative<long long>(b)) return std::get<long long>(a) <= std::get<long long>(b);
		if (std::holds_alternative<double>(b)) return static_cast<double>(std::get<long long>(a)) <= std::get<double>(b);
	}
	// 2. String vs String
	else if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
		return std::get<std::string>(a) <= std::get<std::string>(b);
	}

	// 3. Metamethod __le
	std::vector<LuaValue> res;
	res.reserve(1);

	if (std::holds_alternative<std::shared_ptr<LuaObject>>(a) || std::holds_alternative<std::shared_ptr<LuaObject>>(b)) {
		if (std::holds_alternative<std::shared_ptr<LuaObject>>(a)) {
			auto t = std::get<std::shared_ptr<LuaObject>>(a);
			if (t->metatable) {
				auto le = t->metatable->get_item("__le");
				if (!std::holds_alternative<std::monostate>(le)) {
					call_lua_value(le, res, a, b);
					return !res.empty() && is_lua_truthy(res[0]);
				}
			}
		}
		if (std::holds_alternative<std::shared_ptr<LuaObject>>(b)) {
			auto t = std::get<std::shared_ptr<LuaObject>>(b);
			if (t->metatable) {
				auto le = t->metatable->get_item("__le");
				if (!std::holds_alternative<std::monostate>(le)) {
					call_lua_value(le, res, a, b);
					return !res.empty() && is_lua_truthy(res[0]);
				}
			}
		}
		// Fallback to __lt: a <= b  <==> not (b < a)
		return !lua_less_than(b, a); 
	}

	return !lua_less_than(b, a);
}

bool lua_greater_than(const LuaValue& a, const LuaValue& b) { return lua_less_than(b, a); }
bool lua_greater_equals(const LuaValue& a, const LuaValue& b) { return lua_less_equals(b, a); }
bool lua_not_equals(const LuaValue& a, const LuaValue& b) { return !lua_equals(a, b); }

bool lua_equals(const LuaValue& a, const LuaValue& b) {
	if (a.index() != b.index()) {
		if (std::holds_alternative<double>(a) && std::holds_alternative<long long>(b)) 
			return std::get<double>(a) == static_cast<double>(std::get<long long>(b));
		if (std::holds_alternative<long long>(a) && std::holds_alternative<double>(b)) 
			return static_cast<double>(std::get<long long>(a)) == std::get<double>(b);
		return false;
	}
	
	if (std::holds_alternative<std::monostate>(a)) return true;
	if (std::holds_alternative<bool>(a)) return std::get<bool>(a) == std::get<bool>(b);
	if (std::holds_alternative<double>(a)) return std::get<double>(a) == std::get<double>(b);
	if (std::holds_alternative<long long>(a)) return std::get<long long>(a) == std::get<long long>(b);
	if (std::holds_alternative<std::string>(a)) return std::get<std::string>(a) == std::get<std::string>(b);
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(a)) return std::get<std::shared_ptr<LuaObject>>(a) == std::get<std::shared_ptr<LuaObject>>(b);
	
	// Coroutines are equal if they are the same pointer
	if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(a)) {
		return std::get<std::shared_ptr<LuaCoroutine>>(a) == std::get<std::shared_ptr<LuaCoroutine>>(b);
	}
	
	// Functions logic depends on implementation, usually pointer equality
	return false;
}

LuaValue lua_concat(const LuaValue& a, const LuaValue& b) {
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(a)) {
		auto t = std::get<std::shared_ptr<LuaObject>>(a);
		if (t->metatable) {
			 auto concat = t->metatable->get_item("__concat");
			 if (!std::holds_alternative<std::monostate>(concat)) {
				 std::vector<LuaValue> res;
				 call_lua_value(concat, res, a, b);
				 return res.empty() ? LuaValue(std::monostate{}) : res[0];
			 }
		}
	}
	return LuaValue(to_cpp_string(a) + to_cpp_string(b));
}

// ==========================================
// Standard Library
// ==========================================

void lua_rawget(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args < 2) throw std::runtime_error("bad argument #1 to 'rawget' (table expected)");
	auto table = get_object(args[0]);
	auto key = args[1];

	long long int_key;
	if (std::holds_alternative<long long>(key)) {
		if (table->array_properties.count(std::get<long long>(key))) {
			out.assign({table->array_properties.at(std::get<long long>(key))});
			return;
		}
	} else if (std::holds_alternative<double>(key) && is_integer_key(std::get<double>(key), int_key)) {
		if (table->array_properties.count(int_key)) {
			out.assign({table->array_properties.at(int_key)});
			return;
		}
	} else {
		std::string s_key = value_to_key_string(key);
		if (table->properties.count(s_key)) {
			out.assign({table->properties.at(s_key)});
			return;
		}
	}
}

void lua_rawset(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args < 3) throw std::runtime_error("bad argument #1 to 'rawset' (table expected)");
	auto table = get_object(args[0]);
	auto key = args[1];
	auto value = args[2];
	
	if (std::holds_alternative<std::monostate>(key)) throw std::runtime_error("table index is nil");

	long long int_key;
	if (std::holds_alternative<long long>(key)) {
		if (std::holds_alternative<std::monostate>(value)) table->array_properties.erase(std::get<long long>(key));
		else table->array_properties[std::get<long long>(key)] = value;
	}
	else if (std::holds_alternative<double>(key) && is_integer_key(std::get<double>(key), int_key)) {
		if (std::holds_alternative<std::monostate>(value)) table->array_properties.erase(int_key);
		else table->array_properties[int_key] = value;
	}
	else {
		std::string s_key = value_to_key_string(key);
		if (std::holds_alternative<std::monostate>(value)) table->properties.erase(s_key);
		else table->properties[s_key] = value;
	}
	
	out.assign({args[0]});
}

void lua_rawlen(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args < 1) { out.assign({0.0}); return; }
	
	LuaValue v = args[0];
	if (std::holds_alternative<std::string>(v)) {
		out.assign({static_cast<double>(std::get<std::string>(v).length())});
		return;
	}
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(v)) {
		auto t = std::get<std::shared_ptr<LuaObject>>(v);
		if (t->array_properties.empty()) { out.assign({0.0}); return; }
		out.assign({static_cast<double>(t->array_properties.rbegin()->first)});
		return;
	}
	out.assign({0.0});
}

void lua_rawequal(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args < 2) { out.assign({false}); return; }
	
	LuaValue a = args[0];
	LuaValue b = args[1];
	
	// Exact type match required for rawequal
	if (a.index() != b.index()) { out.assign({false}); return; }

	if (std::holds_alternative<std::monostate>(a)) { out.assign({true}); return; }
	if (std::holds_alternative<bool>(a)) { out.assign({std::get<bool>(a) == std::get<bool>(b)}); return; }
	if (std::holds_alternative<double>(a)) { out.assign({std::get<double>(a) == std::get<double>(b)}); return; }
	if (std::holds_alternative<long long>(a)) { out.assign({std::get<long long>(a) == std::get<long long>(b)}); return; }
	if (std::holds_alternative<std::string>(a)) { out.assign({std::get<std::string>(a) == std::get<std::string>(b)}); return; }
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(a)) { out.assign({std::get<std::shared_ptr<LuaObject>>(a) == std::get<std::shared_ptr<LuaObject>>(b)}); return; }
	
	out.assign({false});
}

void lua_select(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args < 1) throw std::runtime_error("bad argument #1 to 'select' (value expected)");
	LuaValue index_val = args[0];
	int count = n_args - 1;

	if (std::holds_alternative<std::string>(index_val) && std::get<std::string>(index_val) == "#") {
		out.assign({static_cast<double>(count)});
		return;
	}

	long long n = get_long_long(index_val);
	if (n < 0) n = count + n + 1;
	if (n < 1) n = 1;

	// Copy from args[n] to end
	out.clear();
	for (int i = n; i <= count; ++i) {
		out.push_back(args[i]);
	}
}

void lua_next(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args < 1) throw std::runtime_error("bad argument #1 to 'next' (table expected)");
	auto table = get_object(args[0]);
	LuaValue key = (n_args > 1) ? args[1] : LuaValue(std::monostate{});
	
	// 1. Traverse Array part
	auto arr_it = table->array_properties.begin();
	bool in_array = false;
	
	if (std::holds_alternative<std::monostate>(key)) {
		// Start at beginning of array
		if (!table->array_properties.empty()) {
			out.assign({static_cast<double>(arr_it->first), arr_it->second});
			return;
		}
	} else {
		// Try to find key in array
		long long int_key;
		if (std::holds_alternative<long long>(key)) {
			int_key = std::get<long long>(key);
			in_array = true;
		} else if (std::holds_alternative<double>(key) && is_integer_key(std::get<double>(key), int_key)) {
			in_array = true;
		}

		if (in_array) {
			arr_it = table->array_properties.find(int_key);
			if (arr_it != table->array_properties.end()) {
				++arr_it; // Move next
				if (arr_it != table->array_properties.end()) {
					out.assign({static_cast<double>(arr_it->first), arr_it->second});
					return;
				}
			}
		}
	}

	// 2. Traverse Hash part
	auto hash_it = table->properties.begin();
	
	if (!std::holds_alternative<std::monostate>(key) && !in_array) {
		std::string str_key = value_to_key_string(key);
		hash_it = table->properties.find(str_key);
		if (hash_it != table->properties.end()) {
			++hash_it;
		}
	}

	if (hash_it != table->properties.end()) {
		out.assign({hash_it->first, hash_it->second});
		return;
	}
}

void pairs_iterator(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	// Delegates to lua_next
	lua_next(args, n_args, out);
}

void lua_pairs(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args < 1) throw std::runtime_error("bad argument #1 to 'pairs' (table expected)");
	auto table = get_object(args[0]);
	
	out.clear();

	if (table->metatable) {
		auto m = table->metatable->get_item("__pairs");
		if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(m)) {
			LuaValue arg_val = table;
			std::get<std::shared_ptr<LuaFunctionWrapper>>(m)->func(&arg_val, 1, out);
			return;
		}
	}
	
	out.push_back(std::make_shared<LuaFunctionWrapper>(pairs_iterator));
	out.push_back(args[0]);
	out.push_back(std::monostate{});
}

void ipairs_iterator(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.clear();
	if (n_args < 2) { out.push_back(std::monostate{}); return; }
	
	auto table = get_object(args[0]);
	long long index = get_long_long(args[1]) + 1;
	
	LuaValue val = table->get_item(static_cast<double>(index));
	
	if (std::holds_alternative<std::monostate>(val)) {
		out.push_back(std::monostate{});
	} else {
		out.push_back(static_cast<double>(index));
		out.push_back(val);
	}
}

void lua_ipairs(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args < 1) throw std::runtime_error("bad argument #1 to 'ipairs' (table expected)");
	auto table = get_object(args[0]);

	out.clear();

	if (table->metatable) {
		auto m = table->metatable->get_item("__ipairs");
		if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(m)) {
			LuaValue arg_val = table;
			std::get<std::shared_ptr<LuaFunctionWrapper>>(m)->func(&arg_val, 1, out);
			return;
		}
	}
	
	out.push_back(std::make_shared<LuaFunctionWrapper>(ipairs_iterator));
	out.push_back(args[0]);
	out.push_back(0.0);
}

void lua_tonumber(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	// tonumber implementation
	LuaValue val = args[0];

	if (const double* v = std::get_if<double>(&val)) {
		out.assign({*v});
		return;
	}
	
	if (const long long* v = std::get_if<long long>(&val)) {
		out.assign({static_cast<double>(*v)});
		return;
	}
	
	if (const std::string* pStr = std::get_if<std::string>(&val)) {
		const std::string& s = *pStr;
		
		if (s.empty()) return out.assign({0.0});

		// 3. Use std::from_chars (C++17). 
		// It does not allocate, does not throw, and ignores locale.
		double result;
		const char* start = s.data();
		const char* end = start + s.size();
		
		// Note: from_chars parses standard floats (including negatives and scientific notation).
		// If strict '0-9.' only filtering is required, you can check *start here manually.
		auto [ptr, ec] = std::from_chars(start, end, result);

		if (ec == std::errc()) {
			out.assign({result});
			return;
		}
	}

	out.assign({std::monostate{}}); return; // nil
}

// ==========================================
// Runtime Execution Helpers
// ==========================================

inline void call_lua_value(const LuaValue& callable, const LuaValue* args, size_t n_args, std::vector<LuaValue>& out_result) {
	out_result.clear(); 

	if (const auto* wrapper = std::get_if<std::shared_ptr<LuaFunctionWrapper>>(&callable)) {
		(*wrapper)->func(args, n_args, out_result);
		return;
	} 

	// 2. Metatable / __call Handling
	if (const auto* obj = std::get_if<std::shared_ptr<LuaObject>>(&callable)) {
		const auto& t = *obj;
		if (t->metatable) {
			LuaValue call_handler = t->metatable->get_item("__call");
			if (is_lua_truthy(call_handler)) { 
				std::vector<LuaValue> new_args_vec;
				new_args_vec.reserve(n_args + 1);
				new_args_vec.push_back(callable); // Push 'self'
				new_args_vec.insert(new_args_vec.end(), args, args + n_args);
				call_lua_value(call_handler, new_args_vec.data(), new_args_vec.size(), out_result);
				return;
			}
		}
	}
	throw std::runtime_error("attempt to call a " + get_lua_type_name(callable) + " value");
}

void lua_xpcall(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args < 2) throw std::runtime_error("bad argument #2 to 'xpcall' (value expected)");
	LuaValue func = args[0];
	LuaValue errh = args[1];

	out.clear();

	try {
		// Execute
		std::vector<LuaValue> res; 
		// Note: we can't write directly to 'out' yet because on error we need to clear it, 
		// and on success we need to prepend 'true'.
		
		call_lua_value(func, args + 2, n_args - 2, res);
		
		out.reserve(res.size() + 1);
		out.push_back(true);
		out.insert(out.end(), res.begin(), res.end());
	} catch (const std::exception& e) {
		out.clear();
		out.push_back(false);
		
		if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(errh)) {
			LuaValue err_msg = std::string(e.what());
			std::vector<LuaValue> err_res;
			std::get<std::shared_ptr<LuaFunctionWrapper>>(errh)->func(&err_msg, 1, err_res);
			out.push_back(err_res.empty() ? LuaValue(std::monostate{}) : err_res[0]);
		} else {
			out.push_back(std::string(e.what()));
		}
	}
}

void lua_assert(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args < 1) throw std::runtime_error("bad argument #1 to 'assert' (value expected)");
	
	if (!is_lua_truthy(args[0])) {
		LuaValue msg = (n_args > 1) ? args[1] : LuaValue("assertion failed!");
		throw std::runtime_error(std::holds_alternative<std::string>(msg) ? std::get<std::string>(msg) : "assertion failed!");
	}
	
	out.clear();
	for(size_t i = 0; i < n_args; i++) {
		out.push_back(args[i]);
	}
}

void lua_warn(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	for (size_t i = 0; i < n_args; i++) {
		std::cerr << to_cpp_string(args[i]);
	}
	std::cerr << std::endl;
	out.clear();
	out.push_back(std::monostate{});
}

// These are placeholders as requested, implementation depends on FS
void lua_load(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) { throw std::runtime_error("load not supported"); }
void lua_loadfile(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) { throw std::runtime_error("loadfile not supported"); }
void lua_dofile(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) { throw std::runtime_error("dofile not supported"); }
void lua_collectgarbage(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) { out.clear(); out.push_back(std::monostate{}); }

LuaValue lua_get_member(const LuaValue& base, const LuaValue& key) {
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(base)) {
		return std::get<std::shared_ptr<LuaObject>>(base)->get_item(key);
	} else if (std::holds_alternative<std::string>(base)) {
		// String metatable access
		auto s = _G->get_item("string");
		if (std::holds_alternative<std::shared_ptr<LuaObject>>(s)) {
			return std::get<std::shared_ptr<LuaObject>>(s)->get_item(key);
		}
	}
	throw std::runtime_error("attempt to index a " + get_lua_type_name(base) + " value");
}

LuaValue lua_get_length(const LuaValue& val) {
	if (std::holds_alternative<std::string>(val)) {
		return static_cast<double>(std::get<std::string>(val).length());
	}
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) {
		auto obj = std::get<std::shared_ptr<LuaObject>>(val);
		if (obj->metatable) {
			auto len_meta = obj->metatable->get_item("__len");
			if (!std::holds_alternative<std::monostate>(len_meta)) {
				// Internal buffer for length call
				std::vector<LuaValue> res; 
				call_lua_value(len_meta, res, val);
				if (!res.empty()) return res[0];
				return std::monostate{};
			}
		}
		if (obj->array_properties.empty()) return 0.0;
		return static_cast<double>(obj->array_properties.rbegin()->first);
	}
	throw std::runtime_error("attempt to get length of a " + get_lua_type_name(val) + " value");
}