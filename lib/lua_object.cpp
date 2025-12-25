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
	const LuaValue* current_key = &key;
	LuaObject* current_obj = this;

	for (int depth = 0; depth < 100; ++depth) {
		// 1. Array Part Dispatch (1-based indexing)
		long long idx = -1;
		bool is_int = false;

		if (auto* i_ptr = std::get_if<long long>(current_key)) {
			idx = *i_ptr;
			is_int = true;
		}
		else if (auto* d_ptr = std::get_if<double>(current_key)) {
			is_int = is_integer_key(*d_ptr, idx);
		}

		if (is_int && idx >= 1 && idx <= static_cast<long long>(current_obj->array_part.size())) {
			const auto& val = current_obj->array_part[idx - 1];
			if (!std::holds_alternative<std::monostate>(val)) return val;
		}

		// 2. Hash Part
		if (auto* s_ptr = std::get_if<std::string>(current_key)) {
			auto it = current_obj->properties.find(*s_ptr);
			if (it != current_obj->properties.end()) return it->second;
		}
		else {
			// Check properties with the actual LuaValue key (supports non-string keys in map)
			auto it = current_obj->properties.find(*current_key);
			if (it != current_obj->properties.end()) return it->second;
		}

		// 3. Metatable __index logic
		if (!current_obj->metatable) break;
		auto it = current_obj->metatable->properties.find("__index");
		if (it == current_obj->metatable->properties.end()) break;

		const LuaValue& index_val = it->second;
		if (auto* next_obj_ptr = std::get_if<std::shared_ptr<LuaObject>>(&index_val)) {
			current_obj = next_obj_ptr->get();
			continue;
		}
		if (auto* func_ptr = std::get_if<std::shared_ptr<LuaFunctionWrapper>>(&index_val)) {
			LuaValue args[] = {current_obj->shared_from_this(), *current_key};
			thread_local std::vector<LuaValue> results;
			results.clear();
			if (results.capacity() < 1) results.reserve(1);
			(*func_ptr)->func(args, 2, results);
			return results.empty() ? LuaValue(std::monostate{}) : std::move(results[0]);
		}
		break;
	}
	return std::monostate{};
}

void LuaObject::set_item(const LuaValue& key, const LuaValue& value) {
	long long idx = -1;
	bool is_int = false;

	if (std::holds_alternative<long long>(key)) {
		idx = std::get<long long>(key);
		is_int = true;
	}
	else if (std::holds_alternative<double>(key)) {
		is_int = is_integer_key(std::get<double>(key), idx);
	}

	// Check existence for __newindex trigger
	bool key_exists = false;
	if (is_int && idx >= 1 && idx <= static_cast<long long>(array_part.size())) {
		key_exists = !std::holds_alternative<std::monostate>(array_part[idx - 1]);
	}
	else {
		key_exists = properties.count(key);
	}

	if (!key_exists && metatable) {
		auto newindex_val = metatable->get_item("__newindex");
		if (!std::holds_alternative<std::monostate>(newindex_val)) {
			if (auto* func_ptr = std::get_if<std::shared_ptr<LuaFunctionWrapper>>(&newindex_val)) {
				std::vector<LuaValue> dummy;
				LuaValue args[] = {shared_from_this(), key, value};
				(*func_ptr)->func(args, 3, dummy);
				return;
			}
			else if (auto* obj_ptr = std::get_if<std::shared_ptr<LuaObject>>(&newindex_val)) {
				(*obj_ptr)->set_item(key, value);
				return;
			}
		}
	}

	// Raw Set Logic
	if (is_int && idx >= 1) {
		// Growth policy: only use vector for "reasonable" indices to avoid huge allocations
		if (idx < 1000000) {
			if (idx > static_cast<long long>(array_part.size())) {
				if (!std::holds_alternative<std::monostate>(value)) {
					array_part.resize(static_cast<size_t>(idx), std::monostate{});
					array_part[idx - 1] = value;
				}
			}
			else {
				array_part[idx - 1] = value;
				// Optional: Shrink vector if setting last elements to nil
				while (!array_part.empty() && std::holds_alternative<std::monostate>(array_part.back())) {
					array_part.pop_back();
				}
			}
			return;
		}
	}

	// Fallback to hash part for non-integers, negative integers, or very sparse large integers
	if (std::holds_alternative<std::monostate>(value)) {
		properties.erase(key);
	}
	else {
		properties[key] = value;
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
	}
	else if (std::holds_alternative<long long>(value)) {
		return std::to_string(std::get<long long>(value));
	}
	else if (std::holds_alternative<std::string>(value)) {
		return std::get<std::string>(value);
	}
	else if (std::holds_alternative<bool>(value)) {
		return std::get<bool>(value) ? "true" : "false";
	}
	else if (std::holds_alternative<std::shared_ptr<LuaObject>>(value)) {
		std::stringstream ss;
		ss << "table: " << std::get<std::shared_ptr<LuaObject>>(value).get();
		return ss.str();
	}
	else if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(value)) {
		return "function";
	}
	else if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(value)) {
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
		if (std::holds_alternative<long long>(b))
			return std::get<double>(a) < static_cast<double>(std::get<long
				long>(b));
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
		if (std::holds_alternative<long long>(b))
			return std::get<double>(a) <= static_cast<double>(std::get<long
				long>(b));
	}
	else if (std::holds_alternative<long long>(a)) {
		if (std::holds_alternative<long long>(b)) return std::get<long long>(a) <= std::get<long long>(b);
		if (std::holds_alternative<double>(b))
			return static_cast<double>(std::get<long long>(a)) <= std::get<
				double>(b);
	}
	// 2. String vs String
	else if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
		return std::get<std::string>(a) <= std::get<std::string>(b);
	}

	// 3. Metamethod __le
	std::vector<LuaValue> res;
	res.reserve(1);

	if (std::holds_alternative<std::shared_ptr<LuaObject>>(a) || std::holds_alternative<std::shared_ptr<
		LuaObject>>(b)) {
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
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(a))
		return std::get<std::shared_ptr<LuaObject>>(a) ==
			std::get<std::shared_ptr<LuaObject>>(b);

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
	const LuaValue& key = args[1];

	long long idx;
	bool is_int = false;

	// Determine if the key is an integer
	if (auto* i_ptr = std::get_if<long long>(&key)) {
		idx = *i_ptr;
		is_int = true;
	}
	else if (auto* d_ptr = std::get_if<double>(&key)) {
		is_int = is_integer_key(*d_ptr, idx);
	}

	// 1. Check Array Part
	if (is_int && idx >= 1 && idx <= static_cast<long long>(table->array_part.size())) {
		out.assign({table->array_part[idx - 1]});
		return;
	}

	// 2. Check Hash Part
	auto it = table->properties.find(key);
	if (it != table->properties.end()) {
		out.assign({it->second});
	}
	else {
		out.assign({std::monostate{}});
	}
}

void lua_rawset(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args < 3) throw std::runtime_error("bad argument #1 to 'rawset' (table expected)");
	auto table = get_object(args[0]);
	const LuaValue& key = args[1];
	const LuaValue& value = args[2];

	if (std::holds_alternative<std::monostate>(key)) {
		throw std::runtime_error("table index is nil");
	}

	long long idx;
	bool is_int = false;
	if (auto* i_ptr = std::get_if<long long>(&key)) {
		idx = *i_ptr;
		is_int = true;
	}
	else if (auto* d_ptr = std::get_if<double>(&key)) {
		is_int = is_integer_key(*d_ptr, idx);
	}

	// 1. Handle Array Part
	if (is_int && idx >= 1) {
		// If it fits in the current vector or is reasonably close (to prevent massive allocations)
		if (idx <= static_cast<long long>(table->array_part.size())) {
			table->array_part[idx - 1] = value;

			// Optimization: If we just set the last element to nil, we can shrink
			while (!table->array_part.empty() && std::holds_alternative<std::monostate>(table->array_part.back())) {
				table->array_part.pop_back();
			}
			out.assign({args[0]});
			return;
		}
		else if (!std::holds_alternative<std::monostate>(value) && idx < 1000000) {
			// Grow vector to accommodate new index
			table->array_part.resize(static_cast<size_t>(idx), std::monostate{});
			table->array_part[idx - 1] = value;
			out.assign({args[0]});
			return;
		}
	}

	// 2. Handle Hash Part (Fallback for non-integers or sparse large integers)
	if (std::holds_alternative<std::monostate>(value)) {
		table->properties.erase(key);
	}
	else {
		table->properties[key] = value;
	}

	out.assign({args[0]});
}

void lua_rawlen(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args < 1) {
		out.assign({0.0});
		return;
	}
	const LuaValue& v = args[0];
	if (auto* s = std::get_if<std::string>(&v)) {
		out.assign({static_cast<double>(s->length())});
	}
	else if (auto* obj = std::get_if<std::shared_ptr<LuaObject>>(&v)) {
		// For vectors, size is a good approximation of Lua's # operator
		out.assign({static_cast<double>((*obj)->array_part.size())});
	}
	else {
		out.assign({0.0});
	}
}

void lua_rawequal(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args < 2) {
		out.assign({false});
		return;
	}

	LuaValue a = args[0];
	LuaValue b = args[1];

	// Exact type match required for rawequal
	if (a.index() != b.index()) {
		out.assign({false});
		return;
	}

	if (std::holds_alternative<std::monostate>(a)) {
		out.assign({true});
		return;
	}
	if (std::holds_alternative<bool>(a)) {
		out.assign({std::get<bool>(a) == std::get<bool>(b)});
		return;
	}
	if (std::holds_alternative<double>(a)) {
		out.assign({std::get<double>(a) == std::get<double>(b)});
		return;
	}
	if (std::holds_alternative<long long>(a)) {
		out.assign({std::get<long long>(a) == std::get<long long>(b)});
		return;
	}
	if (std::holds_alternative<std::string>(a)) {
		out.assign({std::get<std::string>(a) == std::get<std::string>(b)});
		return;
	}
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(a)) {
		out.assign({std::get<std::shared_ptr<LuaObject>>(a) == std::get<std::shared_ptr<LuaObject>>(b)});
		return;
	}

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
	if (n_args < 1) throw std::runtime_error("table expected");
	auto table = std::get<std::shared_ptr<LuaObject>>(args[0]);
	LuaValue key = (n_args > 1) ? args[1] : LuaValue(std::monostate{});

	// 1. Array Part Traversal
	long long idx = 0;
	bool key_is_nil = std::holds_alternative<std::monostate>(key);

	if (key_is_nil) {
		if (!table->array_part.empty()) {
			out.assign({1.0, table->array_part[0]});
			return;
		}
	}
	else {
		long long k_int;
		if (std::holds_alternative<long long>(key)) k_int = std::get<long long>(key);
		else if (std::holds_alternative<double>(key) && is_integer_key(std::get<double>(key), k_int)) {}
		else k_int = -1;

		if (k_int >= 1 && k_int <= (long long)table->array_part.size()) {
			for (size_t i = (size_t)k_int; i < table->array_part.size(); ++i) {
				if (!std::holds_alternative<std::monostate>(table->array_part[i])) {
					out.assign({static_cast<double>(i + 1), table->array_part[i]});
					return;
				}
			}
			// If we finish array part, move to hash part by setting key to nil-equivalent for hash
			key_is_nil = true;
		}
	}

	// 2. Hash Part Traversal
	auto it = table->properties.begin();
	if (!key_is_nil && !std::holds_alternative<long long>(key) && !std::holds_alternative<double>(key)) {
		it = table->properties.find(key);
		if (it != table->properties.end()) ++it;
	}

	if (it != table->properties.end()) {
		out.assign({it->first, it->second});
	}
	else {
		out.clear(); // End of traversal
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
	auto table = std::get<std::shared_ptr<LuaObject>>(args[0]);
	long long next_idx = static_cast<long long>(std::get<double>(args[1])) + 1;

	if (next_idx >= 1 && next_idx <= (long long)table->array_part.size()) {
		const auto& val = table->array_part[next_idx - 1];
		if (!std::holds_alternative<std::monostate>(val)) {
			out.push_back(static_cast<double>(next_idx));
			out.push_back(val);
			return;
		}
	}
	out.push_back(std::monostate{});
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

	out.assign({std::monostate{}});
	return; // nil
}

// ==========================================
// Runtime Execution Helpers
// ==========================================

inline void call_lua_value(const LuaValue& callable, const LuaValue* args, size_t n_args,
                           std::vector<LuaValue>& out_result) {
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
	}
	catch (const std::exception& e) {
		out.clear();
		out.push_back(false);

		if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(errh)) {
			LuaValue err_msg = std::string(e.what());
			std::vector<LuaValue> err_res;
			std::get<std::shared_ptr<LuaFunctionWrapper>>(errh)->func(&err_msg, 1, err_res);
			out.push_back(err_res.empty() ? LuaValue(std::monostate{}) : err_res[0]);
		}
		else {
			out.push_back(std::string(e.what()));
		}
	}
}

void lua_assert(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args < 1) throw std::runtime_error("bad argument #1 to 'assert' (value expected)");

	if (!is_lua_truthy(args[0])) {
		LuaValue msg = (n_args > 1) ? args[1] : LuaValue("assertion failed!");
		throw std::runtime_error(std::holds_alternative<std::string>(msg)
			                         ? std::get<std::string>(msg)
			                         : "assertion failed!");
	}

	out.clear();
	for (size_t i = 0; i < n_args; i++) {
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
void lua_load(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("load not supported");
}

void lua_loadfile(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("loadfile not supported");
}

void lua_dofile(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	throw std::runtime_error("dofile not supported");
}

void lua_collectgarbage(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.clear();
	out.push_back(std::monostate{});
}

LuaValue lua_get_member(const LuaValue& base, const LuaValue& key) {
	switch (base.index()) {
	case INDEX_OBJECT: {
		return std::get<INDEX_OBJECT>(base)->get_item(key);
	}
	case INDEX_STRING: {
		// Using a cached reference to the string metatable is critical
		static const auto& string_lib = _G->get_item("string");
		return std::get<INDEX_OBJECT>(string_lib)->get_item(key);
	}
	default:
		throw std::runtime_error("attempt to index a " + get_lua_type_name(base) + " value");
	}
}

LuaValue lua_get_length(const LuaValue& val) {
	if (auto* s = std::get_if<std::string>(&val)) return static_cast<double>(s->length());
	if (auto* obj_ptr = std::get_if<std::shared_ptr<LuaObject>>(&val)) {
		auto& obj = *obj_ptr;
		if (obj->metatable) {
			auto len_meta = obj->metatable->get_item("__len");
			if (!std::holds_alternative<std::monostate>(len_meta)) {
				std::vector<LuaValue> res;
				call_lua_value(len_meta, &val, 1, res);
				return res.empty() ? std::monostate{} : res[0];
			}
		}
		return static_cast<double>(obj->array_part.size());
	}
	throw std::runtime_error("attempt to get length of a " + get_lua_type_name(val) + " value");
}
