#include "lua_object.hpp"
#include <iostream>
#include <sstream>
#include <variant>
#include <algorithm>
#include <iomanip>
#include <limits>

// Forward declarations
std::string get_lua_type_name(const LuaValue& val);
std::string to_cpp_string(const LuaValue& value);
bool lua_equals(const LuaValue& a, const LuaValue& b);
bool is_integer_key(double d, long long& out);


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
	return get_item(key);
}

void LuaObject::set(const std::string& key, const LuaValue& value) {
	set_item(key, value);
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
			return std::get<std::shared_ptr<LuaObject>>(index_val)->get_item(key);
		} else if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(index_val)) {
			auto args = std::make_shared<LuaObject>();
			std::vector<LuaValue> args_vec = {shared_from_this(), key};
			std::vector<LuaValue> results = std::get<std::shared_ptr<LuaFunctionWrapper>>(index_val)->func(args_vec.data(), args_vec.size());
			return results.empty() ? std::monostate{} : results[0];
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
			std::vector<LuaValue> args_vec = {shared_from_this(), key, value};
			std::get<std::shared_ptr<LuaFunctionWrapper>>(newindex_val)->func(args_vec.data(), args_vec.size());
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
// Comparison Logic (Fixed Mixed Types)
// ==========================================

bool operator<=(const LuaValue& lhs, const LuaValue& rhs) {
	// Delegates to Lua comparison logic
	extern bool lua_less_equals(const LuaValue&, const LuaValue&);
	return lua_less_equals(lhs, rhs);
}

bool lua_less_than(const LuaValue& a, const LuaValue& b) {
	// 1. Number vs Number (Mixed types)
	if (std::holds_alternative<double>(a)) {
		if (std::holds_alternative<double>(b)) 
			return std::get<double>(a) < std::get<double>(b);
		if (std::holds_alternative<long long>(b)) 
			return std::get<double>(a) < static_cast<double>(std::get<long long>(b));
	}
	else if (std::holds_alternative<long long>(a)) {
		if (std::holds_alternative<long long>(b)) 
			return std::get<long long>(a) < std::get<long long>(b);
		if (std::holds_alternative<double>(b)) 
			return static_cast<double>(std::get<long long>(a)) < std::get<double>(b);
	}
	// 2. String vs String
	else if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
		return std::get<std::string>(a) < std::get<std::string>(b);
	}
	
	// 3. Metamethod __lt
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(a)) {
		auto t = std::get<std::shared_ptr<LuaObject>>(a);
		if (t->metatable) {
			auto lt = t->metatable->get_item("__lt");
			if (!std::holds_alternative<std::monostate>(lt)) {
				auto res = call_lua_value(lt, a, b);
				return !res.empty() && is_lua_truthy(res[0]);
			}
		}
	}
	// Check 'b' metatable if 'a' didn't handle it
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(b)) {
		auto t = std::get<std::shared_ptr<LuaObject>>(b);
		if (t->metatable) {
			 auto lt = t->metatable->get_item("__lt");
			 if (!std::holds_alternative<std::monostate>(lt)) {
				auto res = call_lua_value(lt, a, b);
				return !res.empty() && is_lua_truthy(res[0]);
			 }
		}
	}

	throw std::runtime_error("attempt to compare " + get_lua_type_name(a) + " with " + get_lua_type_name(b));
}

bool lua_less_equals(const LuaValue& a, const LuaValue& b) {
	// 1. Number vs Number (Mixed types)
	if (std::holds_alternative<double>(a)) {
		if (std::holds_alternative<double>(b)) 
			return std::get<double>(a) <= std::get<double>(b);
		if (std::holds_alternative<long long>(b)) 
			return std::get<double>(a) <= static_cast<double>(std::get<long long>(b));
	}
	else if (std::holds_alternative<long long>(a)) {
		if (std::holds_alternative<long long>(b)) 
			return std::get<long long>(a) <= std::get<long long>(b);
		if (std::holds_alternative<double>(b)) 
			return static_cast<double>(std::get<long long>(a)) <= std::get<double>(b);
	}
	// 2. String vs String
	else if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
		return std::get<std::string>(a) <= std::get<std::string>(b);
	}

	// 3. Metamethod __le
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(a) || std::holds_alternative<std::shared_ptr<LuaObject>>(b)) {
		// Try __le on 'a'
		if (std::holds_alternative<std::shared_ptr<LuaObject>>(a)) {
			auto t = std::get<std::shared_ptr<LuaObject>>(a);
			if (t->metatable) {
				auto le = t->metatable->get_item("__le");
				if (!std::holds_alternative<std::monostate>(le)) {
					auto res = call_lua_value(le, a, b);
					return !res.empty() && is_lua_truthy(res[0]);
				}
			}
		}
		// Try __le on 'b'
		if (std::holds_alternative<std::shared_ptr<LuaObject>>(b)) {
			auto t = std::get<std::shared_ptr<LuaObject>>(b);
			if (t->metatable) {
				auto le = t->metatable->get_item("__le");
				if (!std::holds_alternative<std::monostate>(le)) {
					auto res = call_lua_value(le, a, b);
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
	// Different Types?
	if (a.index() != b.index()) {
		if (std::holds_alternative<double>(a) && std::holds_alternative<long long>(b)) 
			return std::get<double>(a) == static_cast<double>(std::get<long long>(b));
		if (std::holds_alternative<long long>(a) && std::holds_alternative<double>(b)) 
			return static_cast<double>(std::get<long long>(a)) == std::get<double>(b);
		return false;
	}
	
	// Same Types
	if (std::holds_alternative<std::monostate>(a)) return true;
	if (std::holds_alternative<bool>(a)) return std::get<bool>(a) == std::get<bool>(b);
	if (std::holds_alternative<double>(a)) return std::get<double>(a) == std::get<double>(b);
	if (std::holds_alternative<long long>(a)) return std::get<long long>(a) == std::get<long long>(b);
	if (std::holds_alternative<std::string>(a)) return std::get<std::string>(a) == std::get<std::string>(b);
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(a)) return std::get<std::shared_ptr<LuaObject>>(a) == std::get<std::shared_ptr<LuaObject>>(b);
	if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(a)) return std::get<std::shared_ptr<LuaFunctionWrapper>>(a) == std::get<std::shared_ptr<LuaFunctionWrapper>>(b);

	return false;
}

LuaValue lua_concat(const LuaValue& a, const LuaValue& b) {
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(a)) {
		auto t = std::get<std::shared_ptr<LuaObject>>(a);
		if (t->metatable) {
			 auto concat = t->metatable->get_item("__concat");
			 if (!std::holds_alternative<std::monostate>(concat)) {
				 return call_lua_value(concat, a, b)[0];
			 }
		}
	}
	return LuaValue(to_cpp_string(a) + to_cpp_string(b));
}

// ==========================================
// Standard Library
// ==========================================

// ==========================================
// Standard Library
// ==========================================

std::vector<LuaValue> lua_rawget(const LuaValue* args, size_t n_args) {
	if (n_args < 2) throw std::runtime_error("bad argument #1 to 'rawget' (table expected)");
	auto table = get_object(args[0]);
	auto key = args[1];
	if (!table) throw std::runtime_error("bad argument #1 to 'rawget' (table expected)");

	long long int_key;
	if (std::holds_alternative<long long>(key)) {
		if (table->array_properties.count(std::get<long long>(key))) 
			return {table->array_properties.at(std::get<long long>(key))};
	} else if (std::holds_alternative<double>(key) && is_integer_key(std::get<double>(key), int_key)) {
		if (table->array_properties.count(int_key)) 
			return {table->array_properties.at(int_key)};
	} else {
		std::string s_key = value_to_key_string(key);
		if (table->properties.count(s_key)) 
			return {table->properties.at(s_key)};
	}
	return {std::monostate{}};
}

std::vector<LuaValue> lua_rawset(const LuaValue* args, size_t n_args) {
	if (n_args < 3) throw std::runtime_error("bad argument #1 to 'rawset' (table expected)");
	auto table = get_object(args[0]);
	auto key = args[1];
	auto value = args[2];
	if (!table) throw std::runtime_error("bad argument #1 to 'rawset' (table expected)");
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
	return {args[0]};
}

std::vector<LuaValue> lua_rawlen(const LuaValue* args, size_t n_args) {
	if (n_args < 1) return {0.0};
	LuaValue v = args[0];
	if (std::holds_alternative<std::string>(v)) return {static_cast<double>(std::get<std::string>(v).length())};
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(v)) {
		auto t = std::get<std::shared_ptr<LuaObject>>(v);
		if (t->array_properties.empty()) return {0.0};
		return {static_cast<double>(t->array_properties.rbegin()->first)};
	}
	return {0.0};
}

std::vector<LuaValue> lua_rawequal(const LuaValue* args, size_t n_args) {
	if (n_args < 2) return {false};
	// Delegates to a simplified check
	LuaValue a = args[0];
	LuaValue b = args[1];
	if (a.index() != b.index()) return {false};

	// Manual check to avoid recursion into metamethods
	if (std::holds_alternative<std::monostate>(a)) return {true};
	if (std::holds_alternative<bool>(a)) return {std::get<bool>(a) == std::get<bool>(b)};
	if (std::holds_alternative<double>(a)) return {std::get<double>(a) == std::get<double>(b)};
	if (std::holds_alternative<long long>(a)) return {std::get<long long>(a) == std::get<long long>(b)};
	if (std::holds_alternative<std::string>(a)) return {std::get<std::string>(a) == std::get<std::string>(b)};
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(a)) return {std::get<std::shared_ptr<LuaObject>>(a) == std::get<std::shared_ptr<LuaObject>>(b)};
	return {false};
}

std::vector<LuaValue> lua_select(const LuaValue* args, size_t n_args) {
	if (n_args < 1) throw std::runtime_error("bad argument #1 to 'select' (value expected)");
	LuaValue index_val = args[0];
	int count = n_args - 1;

	if (std::holds_alternative<std::string>(index_val) && std::get<std::string>(index_val) == "#") {
		return {static_cast<double>(count)};
	}

	long long n = get_long_long(index_val);
	if (n < 0) n = count + n + 1;
	if (n < 1) n = 1;

	std::vector<LuaValue> results;
	for (int i = n; i <= count; ++i) {
		results.push_back(args[i]);
	}
	return results;
}

std::vector<LuaValue> lua_next(const LuaValue* args, size_t n_args) {
	if (n_args < 1) throw std::runtime_error("bad argument #1 to 'next' (table expected)");
	auto table = get_object(args[0]);
	if (!table) throw std::runtime_error("bad argument #1 to 'next' (table expected)");
	LuaValue key = (n_args > 1) ? args[1] : LuaValue(std::monostate{});
	
	if (std::holds_alternative<std::monostate>(key)) {
		if (!table->array_properties.empty()) {
			auto it = table->array_properties.begin();
			return {static_cast<double>(it->first), it->second};
		}
	} else {
		long long int_key;
		bool is_int = false;
		if (std::holds_alternative<long long>(key)) { int_key = std::get<long long>(key); is_int = true; }
		else if (std::holds_alternative<double>(key) && is_integer_key(std::get<double>(key), int_key)) { is_int = true; }

		if (is_int)
		{
			auto it = table->array_properties.find(int_key);
			if (it != table->array_properties.end()) {
				++it;
				if (it != table->array_properties.end()) return {static_cast<double>(it->first), it->second};
			}
		}
	}

	auto it = table->properties.begin();
	if (!std::holds_alternative<std::monostate>(key)) {
		std::string str_key = value_to_key_string(key);
		it = table->properties.find(str_key);
		if (it != table->properties.end()) ++it;
	}
	if (it != table->properties.end()) return {it->first, it->second};

	return {std::monostate{}};
}

std::vector<LuaValue> lua_pairs(const LuaValue* args, size_t n_args) {
	if (n_args < 1) throw std::runtime_error("bad argument #1 to 'pairs' (table expected)");
	auto table = get_object(args[0]);
	if (!table) throw std::runtime_error("bad argument #1 to 'pairs' (table expected)");
	if (table->metatable) {
		auto m = table->metatable->get_item("__pairs");
		if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(m)) {
			// Pass raw pointer
			LuaValue arg_val = table;
			return std::get<std::shared_ptr<LuaFunctionWrapper>>(m)->func(&arg_val, 1);
		}
	}
	return {std::make_shared<LuaFunctionWrapper>(lua_next), table, std::monostate{}};
}

std::vector<LuaValue> ipairs_iterator(const LuaValue* args, size_t n_args) {
	if (n_args < 2) return {std::monostate{}};
	auto table = get_object(args[0]);
	long long index = get_long_long(args[1]) + 1;
	LuaValue val = table->get_item(static_cast<double>(index));
	if (std::holds_alternative<std::monostate>(val)) return {std::monostate{}};
	return {static_cast<double>(index), val};
}

std::vector<LuaValue> lua_ipairs(const LuaValue* args, size_t n_args) {
	if (n_args < 1) throw std::runtime_error("bad argument #1 to 'ipairs' (table expected)");
	auto table = get_object(args[0]);
	if (!table) throw std::runtime_error("bad argument #1 to 'ipairs' (table expected)");
	if (table->metatable) {
		auto m = table->metatable->get_item("__ipairs");
		if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(m)) {
			LuaValue arg_val = table;
			return std::get<std::shared_ptr<LuaFunctionWrapper>>(m)->func(&arg_val, 1);
		}
	}
	return {std::make_shared<LuaFunctionWrapper>(ipairs_iterator), table, 0.0};
}

// ==========================================
// Runtime Execution Helpers
// ==========================================

std::vector<LuaValue> call_lua_value(const LuaValue& callable, const LuaValue* args, size_t n_args) {
	if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(callable)) {
		return std::get<std::shared_ptr<LuaFunctionWrapper>>(callable)->func(args, n_args);
	}
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(callable)) {
		auto t = std::get<std::shared_ptr<LuaObject>>(callable);
		if (t->metatable) {
			auto call = t->metatable->get_item("__call");
			if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(call)) {
				return std::get<std::shared_ptr<LuaFunctionWrapper>>(call)->func(args, n_args);
			}
		}
	}
	throw std::runtime_error("attempt to call a " + get_lua_type_name(callable) + " value");
}

std::vector<LuaValue> lua_xpcall(const LuaValue* args, size_t n_args) {
	if (n_args < 2) throw std::runtime_error("bad argument #2 to 'xpcall' (value expected)");
	LuaValue func = args[0];
	LuaValue errh = args[1];

	try {
		std::vector<LuaValue> res = call_lua_value(func, args + 2, n_args - 2);
		res.insert(res.begin(), true);
		return res;
	} catch (const std::exception& e) {
		if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(errh)) {
			LuaValue err_msg = std::string(e.what());
			auto eres = std::get<std::shared_ptr<LuaFunctionWrapper>>(errh)->func(&err_msg, 1);
			return {false, eres.empty() ? LuaValue(std::monostate{}) : eres[0]};
		}
		return {false, std::string(e.what())};
	}
}

std::vector<LuaValue> lua_assert(const LuaValue* args, size_t n_args) {
	if (n_args < 1) throw std::runtime_error("bad argument #1 to 'assert' (value expected)");
	if (!is_lua_truthy(args[0])) {
		LuaValue msg = (n_args > 1) ? args[1] : LuaValue("assertion failed!");
		throw std::runtime_error(std::holds_alternative<std::string>(msg) ? std::get<std::string>(msg) : "assertion failed!");
	}
	std::vector<LuaValue> ret;
	for(size_t i = 0; i < n_args; i++) {
		ret.push_back(args[i]);
	}
	return ret;
}

std::vector<LuaValue> lua_warn(const LuaValue* args, size_t n_args) {
	for (size_t i = 0; i < n_args; i++) {
		std::cerr << to_cpp_string(args[i]);
	}
	std::cerr << std::endl;
	return {std::monostate{}};
}

std::vector<LuaValue> lua_load(const LuaValue* args, size_t n_args) { throw std::runtime_error("load not supported"); }
std::vector<LuaValue> lua_loadfile(const LuaValue* args, size_t n_args) { throw std::runtime_error("loadfile not supported"); }
std::vector<LuaValue> lua_dofile(const LuaValue* args, size_t n_args) { throw std::runtime_error("dofile not supported"); }
std::vector<LuaValue> lua_collectgarbage(const LuaValue* args, size_t n_args) { return {std::monostate{}}; }

LuaValue lua_get_member(const LuaValue& base, const LuaValue& key) {
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(base)) {
		return std::get<std::shared_ptr<LuaObject>>(base)->get_item(key);
	} else if (std::holds_alternative<std::string>(base)) {
		auto s = _G->get_item("string");
		if (std::holds_alternative<std::shared_ptr<LuaObject>>(s)) {
			return std::get<std::shared_ptr<LuaObject>>(s)->get_item(key);
		}
	}
	throw std::runtime_error("attempt to index a " + get_lua_type_name(base) + " value");
}

LuaValue lua_get_length(const LuaValue& val) {
	if (std::holds_alternative<std::string>(val)) return static_cast<double>(std::get<std::string>(val).length());
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) {
		auto obj = std::get<std::shared_ptr<LuaObject>>(val);
		if (obj->metatable) {
			auto len_meta = obj->metatable->get_item("__len");
			if (!std::holds_alternative<std::monostate>(len_meta)) {
				auto res = call_lua_value(len_meta, obj);
				if (!res.empty()) return res[0];
				return std::monostate{};
			}
		}
		if (obj->array_properties.empty()) return 0.0;
		return static_cast<double>(obj->array_properties.rbegin()->first);
	}
	throw std::runtime_error("attempt to get length of a " + get_lua_type_name(val) + " value");
}