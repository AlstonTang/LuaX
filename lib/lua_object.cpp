#include "lua_object.hpp"
#include <iostream>
#include <cmath>
#include <sstream>
#include <variant>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <charconv>
#include <deque>
#include "coroutine.hpp" // Ensure full definition of LuaCoroutine is available
#include <unordered_set>

// ==========================================
// String Intern Pool
// ==========================================

struct TransparentStringHash {
	using is_transparent = void;
	size_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
	size_t operator()(const std::string& s) const { return std::hash<std::string>{}(s); }
};

struct TransparentStringEq {
	using is_transparent = void;
	bool operator()(std::string_view lhs, std::string_view rhs) const { return lhs == rhs; }
};

static std::unordered_set<std::string, TransparentStringHash, TransparentStringEq>& get_string_pool() {
	static std::unordered_set<std::string, TransparentStringHash, TransparentStringEq> pool;
	return pool;
}

// Updated function in lua_object.cpp
std::string_view LuaObject::intern(std::string_view sv) {
	if (sv.empty()) return "";
	
	constexpr size_t CACHE_SIZE = 64; 
	struct CacheEntry { std::string_view key; std::string_view val; };
	thread_local CacheEntry cache[CACHE_SIZE] = {};
	
	size_t h = std::hash<std::string_view>{}(sv);
	size_t idx = h % CACHE_SIZE;
	
	// 1. Ultra-fast MRU check
	if (cache[idx].key.data() == sv.data() && cache[idx].key.size() == sv.size()) [[likely]] 
		return cache[idx].val;
	if (cache[idx].key == sv) [[likely]] return cache[idx].val;

	auto& pool = get_string_pool();
	
	// 2. Transparent lookup (avoids std::string allocation)
	auto it_pool = pool.find(sv);
	if (it_pool != pool.end()) {
		std::string_view pooled_view = *it_pool;
		cache[idx] = {pooled_view, pooled_view};
		return pooled_view;
	}
	
	// 3. Slow path: Only allocate string when it's genuinely new
	auto [inserted_it, success] = pool.insert(std::string(sv));
	std::string_view pooled_view = *inserted_it;
	cache[idx] = {pooled_view, pooled_view};
	return pooled_view;
}

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

const LuaValue& LuaObject::get_single_char(unsigned char c) {
	struct CharCache {
		char chars[256];
		LuaValue values[256];

		CharCache() {
			for (int i = 0; i < 256; ++i) {
				chars[i] = static_cast<char>(i);
				values[i] = LuaValue(std::string_view(&chars[i], 1));
			}
		}
	};
	static const CharCache cache;

	return cache.values[c];
}

std::shared_ptr<LuaObject> LuaObject::create(
	std::initializer_list<PropPair> props,
	std::initializer_list<LuaValue> arr,
	std::shared_ptr<LuaObject> mt) {
	auto obj = std::allocate_shared<LuaObject>(PoolAllocator<LuaObject>{});
	if (props.size() > SMALL_TABLE_THRESHOLD) {
		obj->properties = std::make_unique<PropMap>();
		for (const auto& p : props) (*obj->properties)[intern_key(p.first)] = p.second;
	} else {
		obj->small_props.reserve(props.size());
		for (const auto& p : props) obj->small_props.push_back({intern_key(p.first), p.second});
	}
	// Convert initializer_list to vector for array_part
	obj->array_part.assign(arr.begin(), arr.end());
	obj->metatable = mt;
	return obj;
}

LuaValue LuaObject::get(std::string_view key) {
	return get_item(key);
}

void LuaObject::set(std::string_view key, const LuaValue& value) {
	set_item(key, value);
}

void LuaObject::set_metatable(const std::shared_ptr<LuaObject>& mt) {
	metatable = mt;
	invalidate_metamethods();
}

LuaValue LuaObject::get_item_internal(const LuaValue& key, int depth) {
	if (depth > 100) [[unlikely]] return std::monostate{};

	// a. Check Array Part
	if (key.index() == INDEX_INTEGER) {
		long long idx = std::get<INDEX_INTEGER>(key);
		if (idx >= 1 && idx <= (long long)array_part.size()) {
			const LuaValue& res = array_part[idx - 1];
			if (res.index() != INDEX_NIL) return res;
		}
	}

	// b. Check Hash Part
	LuaValue* val_ptr = find_prop(key);
	if (val_ptr && val_ptr->index() != INDEX_NIL) return *val_ptr;

	// c. Optimized Metatable Check
	// If no metatable exists, we can stop immediately without checking atomics
	if (!metatable) return std::monostate{};

	auto [idx_meta, _] = get_cached_metamethods();
	if (idx_meta->index() == INDEX_NIL) return std::monostate{};

	if (const auto* next_obj_ptr = std::get_if<std::shared_ptr<LuaObject>>(idx_meta)) {
		return (*next_obj_ptr)->get_item_internal(key, depth + 1);
	} 
	
	if (const auto* func_ptr = std::get_if<std::shared_ptr<LuaCallable>>(idx_meta)) {
		LuaValue args[] = {shared_from_this(), key};
		LuaValueVector results;
		(*func_ptr)->call(args, 2, results);
		return results.empty() ? std::monostate{} : std::move(results[0]);
	}

	return std::monostate{};
}

LuaValue LuaObject::get_item(const LuaValue& key) {
	// Fast dispatch happens BEFORE the lock to avoid deadlock and redundant checks
	switch (key.index()) {
		case INDEX_INTEGER:     return get_item(std::get<INDEX_INTEGER>(key));
		case INDEX_STRING_VIEW: return get_item(std::get<INDEX_STRING_VIEW>(key));
		case INDEX_STRING:      return get_item(std::string_view(std::get<INDEX_STRING>(key)));
		case INDEX_DOUBLE: {
			long long i;
			if (is_integer_key(std::get<double>(key), i)) return get_item(i);
			break;
		}
	}
	return get_item_internal(key, 0);
}

LuaValue LuaObject::get_item(long long idx) {
	return get_item_internal(idx, 0);
}

LuaValue LuaObject::get_item(std::string_view key) {
	// We convert to LuaValue once and pass it down
	return get_item_internal(LuaValue(key), 0);
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

	// 1. Array Part
	if (is_int && idx >= 1) {
		if (idx <= (long long)array_part.size()) {
			array_part[idx - 1] = value;
			if (idx == (long long)array_part.size() && std::holds_alternative<std::monostate>(value)) {
				while (!array_part.empty() && std::holds_alternative<std::monostate>(array_part.back()))
					array_part.pop_back();
			}
			return;
		}
		else if (!std::holds_alternative<std::monostate>(value) && idx < (long long)array_part.size() + 100) {
			array_part.resize(idx, std::monostate{});
			array_part[idx - 1] = value;
			return;
		}
	}

	// 2. Hash Part
	bool key_exists = find_prop(key) != nullptr;

	// 3. Metatable
	if (!key_exists) { // Only check metatable if key doesn't exist in current object
		auto [_, next_meta] = get_cached_metamethods();
		if (next_meta->index() != INDEX_NIL) {
			if (const auto* next_obj_ptr = std::get_if<std::shared_ptr<LuaObject>>(next_meta)) {
				(*next_obj_ptr)->set_item(key, value);
				return;
			}
			if (const auto* func_ptr = std::get_if<std::shared_ptr<LuaCallable>>(next_meta)) {
				LuaValue args[] = {shared_from_this(), key, value};
				LuaValueVector results;
				results.clear();
				(*func_ptr)->call(args, 3, results);
				return;
			}
		}
	}

	set_prop(key, value);
}

void LuaObject::set_item(std::string_view key, const LuaValue& value) {
	// Simplified set_item
	bool key_exists = find_prop(key) != nullptr;

	if (!key_exists) {
		auto [_, next_meta] = get_cached_metamethods();
		if (next_meta->index() != INDEX_NIL) {
			if (const auto* next_obj_ptr = std::get_if<std::shared_ptr<LuaObject>>(next_meta)) {
				(*next_obj_ptr)->set_item(key, value);
				return;
			}
			if (const auto* func_ptr = std::get_if<std::shared_ptr<LuaCallable>>(next_meta)) {
				LuaValue key_val = key; // Use std::string_view directly
				LuaValue args[] = {shared_from_this(), key_val, value};
				LuaValueVector results;
				results.clear();
				(*func_ptr)->call(args, 3, results);
				return;
			}
		}
	}

	set_prop(key, value);
}

void LuaObject::set_item(long long idx, const LuaValue& value) {
	// Simplified set_item
	if (idx >= 1) {
		if (idx <= (long long)array_part.size()) {
			array_part[idx - 1] = value;
			if (idx == (long long)array_part.size() && std::holds_alternative<std::monostate>(value)) {
				while (!array_part.empty() && std::holds_alternative<std::monostate>(array_part.back()))
					array_part.pop_back();
			}
			return;
		} else if (!std::holds_alternative<std::monostate>(value) && idx < (long long)array_part.size() + 100) {
			if (idx > (long long)array_part.size()) array_part.resize((size_t)idx, std::monostate{});
			array_part[idx - 1] = value;
			return;
		}
	}

	LuaValue key = idx;
	bool found = false;
	if (properties) {
		found = properties->find(key) != properties->end();
	} else {
		for (auto& p : small_props) {
			if (p.first.index() == INDEX_INTEGER && std::get<INDEX_INTEGER>(p.first) == idx) {
				found = true; break;
			}
		}
	}

	if (!found && metatable) {
		LuaValue ni_meta = metatable->get_prop("__newindex");
		if (!std::holds_alternative<std::monostate>(ni_meta)) {
			if (const auto* obj_ptr = std::get_if<std::shared_ptr<LuaObject>>(&ni_meta)) {
				(*obj_ptr)->set_item(key, value);
				return;
			}
			if (const auto* func_ptr = std::get_if<std::shared_ptr<LuaCallable>>(&ni_meta)) {
				LuaValue args[] = {shared_from_this(), key, value};
				LuaValueVector results;
				results.clear();
				(*func_ptr)->call(args, 3, results);
				return;
			}
		}
	}

	if (properties) {
		if (std::holds_alternative<std::monostate>(value)) {
			properties->erase(key);
		} else {
			(*properties)[key] = value;
		}
	} else {
		for (auto it = small_props.begin(); it != small_props.end(); ++it) {
			if (it->first.index() == INDEX_INTEGER && std::get<INDEX_INTEGER>(it->first) == idx) {
				if (std::holds_alternative<std::monostate>(value)) {
					small_props.erase(it);
				} else {
					it->second = value;
				}
				return;
			}
		}
		if (!std::holds_alternative<std::monostate>(value)) {
			if (small_props.size() >= SMALL_TABLE_THRESHOLD) {
				properties = std::make_unique<LuaObject::PropMap>();
				for (auto& p : small_props) (*properties)[p.first] = p.second;
				small_props.clear();
				(*properties)[key] = value;
			} else {
				small_props.push_back({key, value});
			}
		}
	}
}

LuaValue LuaObject::get_prop(const LuaValue& key) {
	if (properties) {
		auto it = properties->find(key);
		if (it != properties->end()) return it->second;
	} else {
		for (auto& p : small_props) {
			if (LuaValueEq{}(p.first, key)) return p.second;
		}
	}
	return std::monostate{};
}

LuaValue LuaObject::get_prop(std::string_view key) {
	if (properties) {
		auto it = properties->find(key);
		if (it != properties->end()) return it->second;
	} else {
		for (auto& p : small_props) {
			const auto& k = p.first;
			auto idx = k.index();
			if (idx == INDEX_STRING_VIEW) {
				auto sv = std::get<INDEX_STRING_VIEW>(k);
				if (sv.data() == key.data() && sv.size() == key.size()) [[likely]] return p.second;
				if (sv == key) return p.second;
			} else if (idx == INDEX_STRING) {
				if (std::get<INDEX_STRING>(k) == key) [[likely]] return p.second;
			}
		}
	}
	return std::monostate{};
}

LuaValue* LuaObject::find_prop(const LuaValue& key) {
	if (properties) {
		auto it = properties->find(key);
		if (it != properties->end()) return &it->second;
		return nullptr;
	}
	for (auto& p : small_props) {
		if (LuaValueEq{}(p.first, key)) return &p.second;
	}
	return nullptr;
}

// Updated function in lua_object.cpp
LuaValue* LuaObject::find_prop(std::string_view key) {
	// 1. Check small_props with pointer-first logic
	for (auto& p : small_props) {
		const size_t idx = p.first.index();
		if (idx == INDEX_STRING_VIEW) [[likely]] {
			auto sv = std::get<INDEX_STRING_VIEW>(p.first);
			// If pointers match, it's the same interned string
			if (sv.data() == key.data()) return &p.second;
		} else if (idx == INDEX_STRING) {
			if (std::get<INDEX_STRING>(p.first).data() == key.data()) return &p.second;
		}
	}

	// 2. Check Hash Part (Map)
	if (properties) [[unlikely]] {
		// Because LuaValueHash/Eq are transparent, this uses the string_view 
		// without creating a std::string or LuaValue temporary.
		auto it = properties->find(key); 
		if (it != properties->end()) return &it->second;
	}
	return nullptr;
}

void LuaObject::set_prop(const LuaValue& key, const LuaValue& value) {
	LuaValue interned_key = key;
	if (key.index() == INDEX_STRING) {
		interned_key = intern(std::get<std::string>(key));
	} else if (key.index() == INDEX_STRING_VIEW) {
		interned_key = intern(std::get<std::string_view>(key));
	}

	if (properties) {
		if (std::holds_alternative<std::monostate>(value)) {
			properties->erase(interned_key);
		} else {
			(*properties)[interned_key] = value;
		}
		return;
	}

	LuaValueEq eq;
	for (auto it = small_props.begin(); it != small_props.end(); ++it) {
		if (eq(it->first, interned_key)) {
			if (std::holds_alternative<std::monostate>(value)) {
				small_props.erase(it);
			} else {
				it->second = value;
			}
			return;
		}
	}

	if (!std::holds_alternative<std::monostate>(value)) {
		if (small_props.size() >= SMALL_TABLE_THRESHOLD) {
			properties = std::make_unique<LuaObject::PropMap>(); 
			for (auto& p : small_props) {
				properties->emplace(std::move(p.first), std::move(p.second));
			}
			small_props.clear();
			properties->emplace(std::move(interned_key), std::move(value));
		} else {
			small_props.push_back({interned_key, value});
		}
	}
}

void LuaObject::set_prop(std::string_view key, const LuaValue& value) {
	std::string_view interned = intern(key);

	if (properties) {
		if (std::holds_alternative<std::monostate>(value)) {
			properties->erase(LuaValue(interned));
		} else {
			auto it = properties->find(interned);
			if (it != properties->end()) it->second = value;
			else properties->emplace(LuaValue(interned), value);
		}
		return;
	}

	LuaValueEq eq;
	for (auto it = small_props.begin(); it != small_props.end(); ++it) {
		if (eq(it->first, interned)) {
			if (std::holds_alternative<std::monostate>(value)) {
				small_props.erase(it);
			} else {
				it->second = value;
			}
			return;
		}
	}

	if (!std::holds_alternative<std::monostate>(value)) {
		LuaValue key_val = interned;
		if (small_props.size() >= SMALL_TABLE_THRESHOLD) {
			properties = std::make_unique<LuaObject::PropMap>();
			for (auto& p : small_props) (*properties)[p.first] = p.second;
			small_props.clear();
			(*properties)[key_val] = value;
		} else {
			small_props.push_back({key_val, value});
		}
	}
}

void LuaObject::set_item(const LuaValue& key, const LuaValueVector& value) {
	set_item(key, value.empty() ? LuaValue(std::monostate{}) : value[0]);
}

void LuaObject::table_insert(const LuaValue& value) {
	
	array_part.push_back(value);
}

void LuaObject::table_insert(long long pos, const LuaValue& value) {
	long long current_size = static_cast<long long>(array_part.size());
	if (pos >= 1 && pos <= current_size + 1) {
		
		array_part.insert(array_part.begin() + (pos - 1), value);
	}
}

void lua_table_insert(const LuaValue& t, const LuaValue& v) {
	if (auto obj = std::get_if<std::shared_ptr<LuaObject>>(&t)) {
		(*obj)->table_insert(v);
	}
}

void lua_table_insert(const LuaValue& t, long long pos, const LuaValue& v) {
	if (auto obj = std::get_if<std::shared_ptr<LuaObject>>(&t)) {
		(*obj)->table_insert(pos, v);
	}
}

// ==========================================
// Type Conversions & Printing
// ==========================================

void append_to_string(const LuaValue& value, std::string& out) {
	switch (value.index()) {
		case INDEX_NIL: out.append("nil"); break;
		case INDEX_BOOLEAN: out.append(std::get<bool>(value) ? "true" : "false"); break;
		case INDEX_DOUBLE: {
			char buf[64];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), std::get<double>(value));
			out.append(buf, ptr - buf);
			break;
		}
		case INDEX_INTEGER: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), std::get<long long>(value));
			out.append(buf, ptr - buf);
			break;
		}
		case INDEX_STRING: out.append(std::get<std::string>(value)); break;
		case INDEX_STRING_VIEW: out.append(std::get<std::string_view>(value)); break;
		case INDEX_OBJECT: {
			char buf[32];
			int len = snprintf(buf, sizeof(buf), "table: %p", (void*)std::get<std::shared_ptr<LuaObject>>(value).get());
			out.append(buf, len);
			break;
		}
		case INDEX_FUNCTION: out.append("function"); break;
		case INDEX_COROUTINE: out.append("thread"); break;
		case INDEX_CFUNCTION: out.append("function"); break;
		default: out.append("unknown"); break;
	}
}

std::string to_cpp_string(const LuaValue& value) {
	if (const auto* s = std::get_if<std::string>(&value)) return *s;
	if (const auto* sv = std::get_if<std::string_view>(&value)) return std::string(*sv);
	std::string res;
	res.reserve(32);
	append_to_string(value, res);
	return res;
}

std::string to_cpp_string(const LuaValueVector& value) {
	return value.empty() ? "nil" : to_cpp_string(value[0]);
}

std::string get_lua_type_name(const LuaValue& val) {
	if (std::holds_alternative<std::monostate>(val)) return "nil";
	if (std::holds_alternative<bool>(val)) return "boolean";
	if (std::holds_alternative<double>(val) || std::holds_alternative<long long>(val)) return "number";
	if (std::holds_alternative<std::string>(val) || std::holds_alternative<std::string_view>(val)) return "string";
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) return "table";
	if (std::holds_alternative<std::shared_ptr<LuaCallable>>(val)) return "function";
	if (std::holds_alternative<LuaCFunction>(val)) return "function";
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
	else if ((a.index() == INDEX_STRING || a.index() == INDEX_STRING_VIEW) &&
			 (b.index() == INDEX_STRING || b.index() == INDEX_STRING_VIEW)) {
		std::string_view sa = (a.index() == INDEX_STRING) ? std::string_view(std::get<std::string>(a)) : std::get<std::string_view>(a);
		std::string_view sb = (b.index() == INDEX_STRING) ? std::string_view(std::get<std::string>(b)) : std::get<std::string_view>(b);
		return sa < sb;
	}

	// 3. Metamethod __lt
	// Helper buffer for metamethod calls
	LuaValueVector res;
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
	else if ((a.index() == INDEX_STRING || a.index() == INDEX_STRING_VIEW) &&
			 (b.index() == INDEX_STRING || b.index() == INDEX_STRING_VIEW)) {
		std::string_view sa = (a.index() == INDEX_STRING) ? std::string_view(std::get<std::string>(a)) : std::get<std::string_view>(a);
		std::string_view sb = (b.index() == INDEX_STRING) ? std::string_view(std::get<std::string>(b)) : std::get<std::string_view>(b);
		return sa <= sb;
	}

	// 3. Metamethod __le
	LuaValueVector res;
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

// Updated function in lua_object.cpp
bool lua_equals(const LuaValue& a, const LuaValue& b) {
	size_t lidx = a.index();
	size_t ridx = b.index();

	if (lidx != ridx) {
		// Handle cross-type number equality (1 == 1.0)
		if (lidx == INDEX_DOUBLE && ridx == INDEX_INTEGER)
			return std::get<double>(a) == static_cast<double>(std::get<long long>(b));
		if (lidx == INDEX_INTEGER && ridx == INDEX_DOUBLE)
			return static_cast<double>(std::get<long long>(a)) == std::get<double>(b);
		
		// Handle String vs StringView
		if ((lidx == INDEX_STRING || lidx == INDEX_STRING_VIEW) &&
			(ridx == INDEX_STRING || ridx == INDEX_STRING_VIEW)) {
			std::string_view sa = (lidx == INDEX_STRING) ? std::string_view(std::get<std::string>(a)) : std::get<std::string_view>(a);
			std::string_view sb = (ridx == INDEX_STRING) ? std::string_view(std::get<std::string>(b)) : std::get<std::string_view>(b);
			return sa == sb;
		}
		return false;
	}

	// Same types: Single switch is much faster than multiple holds_alternative
	switch (lidx) {
		case INDEX_NIL:     return true;
		case INDEX_BOOLEAN: return std::get<bool>(a) == std::get<bool>(b);
		case INDEX_DOUBLE:  return std::get<double>(a) == std::get<double>(b);
		case INDEX_INTEGER: return std::get<long long>(a) == std::get<long long>(b);
		case INDEX_STRING_VIEW: {
			auto s1 = std::get<std::string_view>(a);
			auto s2 = std::get<std::string_view>(b);
			return (s1.data() == s2.data() && s1.size() == s2.size()) || (s1 == s2);
		}
		case INDEX_STRING:  return std::get<std::string>(a) == std::get<std::string>(b);
		case INDEX_OBJECT:  return std::get<std::shared_ptr<LuaObject>>(a) == std::get<std::shared_ptr<LuaObject>>(b);
		case INDEX_CFUNCTION: return std::get<LuaCFunction>(a).ptr == std::get<LuaCFunction>(b).ptr;
		default:            return a == b;
	}
}

// Helper to create a non-owning view of a string to avoid copy
LuaValue as_view(const LuaValue& v) {
	if (std::holds_alternative<std::string>(v)) {
		return LuaValue(std::string_view(std::get<std::string>(v)));
	}
	return v; // Copy of non-string is cheap (number, bool) or necessary (table shared_ptr)
}

LuaValue lua_concat(const LuaValue& a, const LuaValue& b) {
	if (const auto* obj_a = std::get_if<std::shared_ptr<LuaObject>>(&a)) {
		if ((*obj_a)->metatable) {
			auto concat = (*obj_a)->metatable->get_item("__concat");
			if (!std::holds_alternative<std::monostate>(concat)) {
				LuaValueVector res;
				call_lua_value(concat, res, a, b);
				return res.empty() ? LuaValue(std::monostate{}) : res[0];
			}
		}
	}
	if (const auto* obj_b = std::get_if<std::shared_ptr<LuaObject>>(&b)) {
		if ((*obj_b)->metatable) {
			auto concat = (*obj_b)->metatable->get_item("__concat");
			if (!std::holds_alternative<std::monostate>(concat)) {
				LuaValueVector res;
				call_lua_value(concat, res, a, b);
				return res.empty() ? LuaValue(std::monostate{}) : res[0];
			}
		}
	}
	
	std::string res;
	res.reserve(64); // Reasonable default
	append_to_string(a, res);
	append_to_string(b, res);
	return res;
}

LuaValue lua_concat(LuaValue&& a, const LuaValue& b) {
	// 1. Try In-Place String Append
	if (auto* s_a = std::get_if<std::string>(&a)) {
		append_to_string(b, *s_a);
		return std::move(a);
	}

	// 2. Metamethods (Copy logic from const& version, but passed a as lvalue to call_lua_value?)
	// call_lua_value takes args by pointer or reference. 
	// If we pass 'a' (rvalue ref) to call_lua_value, it binds to const LuaValue&. Safe.
	
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(a)) {
		auto t = std::get<std::shared_ptr<LuaObject>>(a);
		if (t->metatable) {
			auto concat = t->metatable->get_item("__concat");
			if (!std::holds_alternative<std::monostate>(concat)) {
				LuaValueVector res;
				call_lua_value(concat, res, a, b);
				return res.empty() ? LuaValue(std::monostate{}) : res[0];
			}
		}
	}
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(b)) {
		auto t = std::get<std::shared_ptr<LuaObject>>(b);
		if (t->metatable) {
			auto concat = t->metatable->get_item("__concat");
			if (!std::holds_alternative<std::monostate>(concat)) {
				LuaValueVector res;
				call_lua_value(concat, res, a, b);
				return res.empty() ? LuaValue(std::monostate{}) : res[0];
			}
		}
	}

	// Fallback: If a is not string (e.g. number), we must create new string.
	std::string res;
	res.reserve(32);
	append_to_string(a, res);
	append_to_string(b, res);
	return res;
}

// Optimization: Prepend a value to an existing rvalue string
LuaValue lua_concat(const LuaValue& a, LuaValue&& b) {
	if (auto* s_b = std::get_if<std::string>(&b)) {
		// If 'a' is also a string/number, we can do a smart prepend
		// This is still slightly slower than append, but way faster than a full copy
		std::string res;
		// Optimization: one allocation for the whole thing
		// (Assuming you have a helper to get string length of a)
		// res.reserve(get_length(a) + s_b->size()); 
		
		append_to_string(a, res);
		res += *s_b; // Append the temporary's content
		return res;
	}
	
	// Fallback: Use the standard logic
	return lua_concat(a, static_cast<const LuaValue&>(b));
}

LuaValue lua_concat(LuaValue&& a, LuaValue&& b) {
	// Both are temporaries. We'll pick 'a' to be the primary buffer.
	return lua_concat(std::move(a), static_cast<const LuaValue&>(b));
}

// ==========================================
// Standard Library
// ==========================================

void lua_rawget(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 2) [[unlikely]] throw std::runtime_error("bad argument #1 to 'rawget' (table expected)");
	auto table = get_object(args[0]);
	const LuaValue& key = args[1];

	long long idx;
	bool is_int = false;
	if (auto* i_ptr = std::get_if<long long>(&key)) { idx = *i_ptr; is_int = true; }
	else if (auto* d_ptr = std::get_if<double>(&key)) { is_int = is_integer_key(*d_ptr, idx); }

	if (is_int && idx >= 1 && idx <= (long long)(table->array_part.size())) {
		out.assign({table->array_part[idx - 1]});
		return;
	}

	LuaValue val = table->get_prop(key);
	if (!std::holds_alternative<std::monostate>(val)) {
		out.assign({val});
	} else {
		out.assign({std::monostate{}});
	}
}

void lua_rawset(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 3) [[unlikely]] throw std::runtime_error("bad argument #1 to 'rawset' (table expected)");
	auto table = get_object(args[0]);
	const LuaValue& key = args[1];
	const LuaValue& value = args[2];

	if (std::holds_alternative<std::monostate>(key)) [[unlikely]] throw std::runtime_error("table index is nil");

	long long idx;
	bool is_int = false;
	if (auto* i_ptr = std::get_if<long long>(&key)) { idx = *i_ptr; is_int = true; }
	else if (auto* d_ptr = std::get_if<double>(&key)) { is_int = is_integer_key(*d_ptr, idx); }

	if (is_int && idx >= 1) {
		if (idx <= (long long)table->array_part.size()) {
			table->array_part[idx - 1] = value;
			while (!table->array_part.empty() && std::holds_alternative<std::monostate>(table->array_part.back()))
				table->array_part.pop_back();
			out.assign({args[0]});
			return;
		} else if (!std::holds_alternative<std::monostate>(value) && idx < 1000000) {
			table->array_part.resize(idx, std::monostate{});
			table->array_part[idx - 1] = value;
			out.assign({args[0]});
			return;
		}
	}

	table->set_prop(key, value);
	out.assign({args[0]});
}

void lua_rawlen(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 1) [[unlikely]] {
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

void lua_rawequal(const LuaValue* args, size_t n_args, LuaValueVector& out) {
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

void lua_select(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 1) [[unlikely]] throw std::runtime_error("bad argument #1 to 'select' (value expected)");
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

void lua_next(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 1) [[unlikely]] throw std::runtime_error("table expected");
	auto table = std::get<std::shared_ptr<LuaObject>>(args[0]);
	LuaValue key = (n_args > 1) ? args[1] : LuaValue(std::monostate{});

	// 1. Array Part
	long long k_int;
	bool key_is_nil = std::holds_alternative<std::monostate>(key);
	bool key_is_int = !key_is_nil && (std::holds_alternative<long long>(key) || (std::holds_alternative<double>(key) && is_integer_key(std::get<double>(key), k_int)));

	if (key_is_nil) {
		size_t arr_size = table->array_part.size();
		for (size_t i = 0; i < arr_size; ++i) {
			if (!std::holds_alternative<std::monostate>(table->array_part[i])) {
				out.assign({(double)(i + 1), table->array_part[i]});
				return;
			}
		}
	} else if (key_is_int) {
		if (std::holds_alternative<long long>(key)) k_int = std::get<long long>(key);
		size_t arr_size = table->array_part.size();
		if (k_int >= 1 && k_int <= (long long)arr_size) {
			for (size_t i = (size_t)k_int; i < arr_size; ++i) {
				if (!std::holds_alternative<std::monostate>(table->array_part[i])) {
					out.assign({(double)(i + 1), table->array_part[i]});
					return;
				}
			}
		}
		key_is_nil = true; // Move to hash part
	}

	// 2. Hash Part - Upgrade to full map if using next() for simplicity of traversal
	if (!table->properties) {
		table->properties = std::make_unique<LuaObject::PropMap>();
		for (auto& p : table->small_props) (*table->properties)[p.first] = p.second;
		table->small_props.clear();
	}

	auto it = table->properties->begin();
	if (!key_is_nil) {
		it = table->properties->find(key);
		if (it != table->properties->end()) ++it;
		else throw std::runtime_error("invalid key to 'next'");
	}

	if (it != table->properties->end()) {
		out.assign({it->first, it->second});
	} else {
		out.clear();
	}
}

void pairs_iterator(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	// Delegates to lua_next
	lua_next(args, n_args, out);
}

void lua_pairs(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 1) [[unlikely]] throw std::runtime_error("bad argument #1 to 'pairs' (table expected)");
	auto table = get_object(args[0]);

	out.clear();

	if (table->metatable) {
		auto m = table->metatable->get_item("__pairs");
		if (std::holds_alternative<std::shared_ptr<LuaCallable>>(m)) {
			LuaValue arg_val = table;
			std::get<std::shared_ptr<LuaCallable>>(m)->call(&arg_val, 1, out);
			return;
		}
	}

	static const auto pairs_iter = LUA_C_FUNC(pairs_iterator);
	out.push_back(pairs_iter);
	out.push_back(args[0]);
	out.push_back(std::monostate{});
}

void ipairs_iterator(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.clear();
	auto table = std::get<std::shared_ptr<LuaObject>>(args[0]);
	long long next_idx = static_cast<long long>(std::get<double>(args[1])) + 1;

	size_t arr_size = table->array_part.size();
	if (next_idx >= 1 && next_idx <= (long long)arr_size) {
		const auto& val = table->array_part[next_idx - 1];
		if (!std::holds_alternative<std::monostate>(val)) {
			out.push_back(static_cast<double>(next_idx));
			out.push_back(val);
			return;
		}
	}
	out.push_back(std::monostate{});
}

void lua_ipairs(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 1) [[unlikely]] throw std::runtime_error("bad argument #1 to 'ipairs' (table expected)");
	auto table = get_object(args[0]);

	out.clear();

	if (table->metatable) {
		auto m = table->metatable->get_item("__ipairs");
		if (std::holds_alternative<std::shared_ptr<LuaCallable>>(m)) {
			LuaValue arg_val = table;
			std::get<std::shared_ptr<LuaCallable>>(m)->call(&arg_val, 1, out);
			return;
		}
	}

	static const auto ipairs_iter = LUA_C_FUNC(ipairs_iterator);
	out.push_back(ipairs_iter);
	out.push_back(args[0]);
	out.push_back(0.0);
}

void lua_tonumber(const LuaValue* args, size_t n_args, LuaValueVector& out) {
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

	std::string_view s;
	if (const std::string* pStr = std::get_if<std::string>(&val)) {
		s = *pStr;
	} else if (const std::string_view* pSV = std::get_if<std::string_view>(&val)) {
		s = *pSV;
	} else {
		out.assign({std::monostate{}});
		return;
	}

	if (s.empty()) return out.assign({std::monostate{}});

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

	out.assign({std::monostate{}});
	return; // nil
}

// ==========================================
// Runtime Execution Helpers
// ==========================================

LuaValue LuaCallable::call0() {
	LuaValueVector res;
	call(nullptr, 0, res);
	return res.empty() ? LuaValue(std::monostate{}) : std::move(res[0]);
}

LuaValue LuaCallable::call1(const LuaValue& a1) {
	LuaValueVector res;
	const LuaValue args[] = {a1};
	call(args, 1, res);
	return res.empty() ? LuaValue(std::monostate{}) : std::move(res[0]);
}

LuaValue LuaCallable::call2(const LuaValue& a1, const LuaValue& a2) {
	LuaValueVector res;
	const LuaValue args[] = {a1, a2};
	call(args, 2, res);
	return res.empty() ? LuaValue(std::monostate{}) : std::move(res[0]);
}

LuaValue LuaCallable::call3(const LuaValue& a1, const LuaValue& a2, const LuaValue& a3) {
	LuaValueVector res;
	const LuaValue args[] = {a1, a2, a3};
	call(args, 3, res);
	return res.empty() ? LuaValue(std::monostate{}) : std::move(res[0]);
}

void lua_xpcall(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 2) [[unlikely]] throw std::runtime_error("bad argument #2 to 'xpcall' (value expected)");
	LuaValue func = args[0];
	LuaValue errh = args[1];

	out.clear();

	try {
		// Execute
		LuaValueVector res;
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

		if (std::holds_alternative<std::shared_ptr<LuaCallable>>(errh) || std::holds_alternative<LuaCFunction>(errh)) {
			LuaValue err_msg = std::string(e.what());
			LuaValueVector err_res;
			call_lua_value(errh, &err_msg, 1, err_res);
			out.push_back(err_res.empty() ? LuaValue(std::monostate{}) : err_res[0]);
		}
		else {
			out.push_back(std::string(e.what()));
		}
	}
}

void lua_assert(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 1) [[unlikely]] throw std::runtime_error("bad argument #1 to 'assert' (value expected)");

	if (!is_lua_truthy(args[0])) {
		LuaValue msg = (n_args > 1) ? args[1] : LuaValue(std::string_view("assertion failed!"));
		throw std::runtime_error(std::holds_alternative<std::string>(msg)
									 ? std::get<std::string>(msg)
									 : "assertion failed!");
	}

	out.clear();
	for (size_t i = 0; i < n_args; i++) {
		out.push_back(args[i]);
	}
}

void lua_warn(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	for (size_t i = 0; i < n_args; i++) {
		std::cerr << to_cpp_string(args[i]);
	}
	std::cerr << std::endl;
	out.clear();
	out.push_back(std::monostate{});
}

// These are placeholders as requested, implementation depends on FS
void lua_load(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("load not supported");
}

void lua_loadfile(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("loadfile not supported");
}

void lua_dofile(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	throw std::runtime_error("dofile not supported");
}

void lua_collectgarbage(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.clear();
	out.push_back(std::monostate{});
}

thread_local std::deque<LuaValueVector> _func_ret_buf_stack;
thread_local size_t _func_ret_buf_depth = 0;

LuaValueVector& luax_get_ret_buf() {
	if (_func_ret_buf_depth >= _func_ret_buf_stack.size()) {
		_func_ret_buf_stack.emplace_back();
		_func_ret_buf_stack.back().reserve(8);
	}
	auto& buf = _func_ret_buf_stack[_func_ret_buf_depth++];
	buf.clear();
	return buf;
}

void luax_release_ret_buf() {
	if (_func_ret_buf_depth > 0) {
		_func_ret_buf_depth--;
	}
}

void luax_flush_thread_pool() {
	LuaObjectPool::cleanup();
}

void luax_cleanup() {
	LuaObjectPool::cleanup();
	get_string_pool().clear();
}
