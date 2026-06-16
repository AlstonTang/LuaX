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

const std::string* intern_string_ptr(std::string_view sv);

// ==========================================
// LuaValue Implementations
// ==========================================

LuaValue::LuaValue(LuaObject* obj) : data(TAG_OBJECT | (reinterpret_cast<uint64_t>(obj) & PAYLOAD_MASK)) { retain(); }
LuaValue::LuaValue(LuaCallable* func) : data(TAG_FUNCTION | (reinterpret_cast<uint64_t>(func) & PAYLOAD_MASK)) { retain(); }
LuaValue::LuaValue(LuaCoroutine* coro) : data(TAG_CORO | (reinterpret_cast<uint64_t>(coro) & PAYLOAD_MASK)) { retain(); }

LuaValue::LuaValue(const std::string& s) {
    auto* ls = new LuaString(s);
    data = TAG_STRING | (reinterpret_cast<uint64_t>(ls) & PAYLOAD_MASK);
    ls->retain();
}

LuaValue::LuaValue(std::string_view sv) {
    const std::string* pooled = intern_string_ptr(sv);
    data = TAG_STRING | (reinterpret_cast<uint64_t>(pooled) | 1ULL);
}

size_t LuaValueHash::operator()(const LuaValue& v) const {
    size_t idx = v.index();
    switch (idx) {
        case INDEX_NIL: return 0;
        case INDEX_BOOLEAN: return std::hash<bool>{}(v.get<bool>());
        case INDEX_DOUBLE: 
        case INDEX_INTEGER: return std::hash<double>{}(v.get<double>());
        case INDEX_STRING: return std::hash<std::string_view>{}(v.get<std::string_view>());
        case INDEX_OBJECT: return std::hash<void*>{}(v.get<LuaObject*>());
        case INDEX_FUNCTION: return std::hash<void*>{}(v.get<LuaCallable*>());
        case INDEX_COROUTINE: return std::hash<void*>{}(v.get<LuaCoroutine*>());
        case INDEX_CFUNCTION: return std::hash<void*>{}(reinterpret_cast<void*>(v.raw_data() & PAYLOAD_MASK));
        default: return 0;
    }
}

bool LuaValueEq::operator()(const LuaValue& lhs, std::string_view rhs) const {
    if (lhs.index() == INDEX_STRING) {
        return lhs.get<std::string_view>() == rhs;
    }
    return false;
}

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

#include <mutex>

static std::mutex& get_string_pool_mutex() {
	static std::mutex mtx;
	return mtx;
}

const std::string* intern_string_ptr(std::string_view sv) {
	if (sv.empty()) {
		static const std::string empty;
		return &empty;
	}
	
	constexpr size_t CACHE_SIZE = 64; 
	struct CacheEntry { std::string_view key; const std::string* val; };
	thread_local CacheEntry cache[CACHE_SIZE] = {};
	
	size_t h = std::hash<std::string_view>{}(sv);
	size_t idx = h % CACHE_SIZE;
	
	if (cache[idx].key.data() == sv.data() && cache[idx].key.size() == sv.size()) [[likely]] 
		return cache[idx].val;
	if (cache[idx].key == sv) [[likely]] return cache[idx].val;

	const std::string* pooled_ptr = nullptr;
	{
		std::lock_guard<std::mutex> lock(get_string_pool_mutex());
		auto& pool = get_string_pool();
		auto it = pool.find(sv);
		if (it != pool.end()) {
			pooled_ptr = &(*it);
		} else {
			auto [inserted_it, success] = pool.insert(std::string(sv));
			pooled_ptr = &(*inserted_it);
		}
	}
	
	cache[idx] = {std::string_view(*pooled_ptr), pooled_ptr};
	return pooled_ptr;
}

// Updated function in lua_object.cpp
std::string_view LuaObject::intern(std::string_view sv) {
	const std::string* ptr = intern_string_ptr(sv);
	return std::string_view(*ptr);
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
	switch (key.index()) {
		case INDEX_STRING:
			return std::string(key.get<std::string_view>());
		case INDEX_STRING_VIEW:
			return std::string(key.get<std::string_view>());
		default:
			return to_cpp_string(key);
	}
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

LuaObject* LuaObject::create(
	std::initializer_list<PropPair> props,
	std::initializer_list<LuaValue> arr,
	LuaObject* mt) {
	auto* obj = new LuaObject();
	if (props.size() > SMALL_TABLE_THRESHOLD) {
		obj->properties = std::make_unique<PropMap>();
		for (const auto& p : props) (*obj->properties)[intern_key(p.first)] = p.second;
	} else {
		obj->small_props.reserve(props.size());
		for (const auto& p : props) obj->small_props.push_back({intern_key(p.first), p.second});
	}
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

void LuaObject::set_metatable(LuaObject* mt) {
	metatable = mt;
	invalidate_metamethods();
}

LuaValue LuaObject::get_item_internal(const LuaValue& key, int depth) {
	if (depth > 100) [[unlikely]] return LuaValue();

	// a. Check Array Part
	if (key.index() == INDEX_INTEGER) {
		long long idx = key.get<long long>();
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
	if (!metatable) return LuaValue();

	auto [idx_meta, _] = get_cached_metamethods();
	if (idx_meta->index() == INDEX_NIL) return LuaValue();

	switch (idx_meta->index()) {
		case INDEX_OBJECT: {
			auto* next_obj = idx_meta->get<LuaObject*>();
			return next_obj->get_item_internal(key, depth + 1);
		}
		case INDEX_FUNCTION: {
			auto* func = idx_meta->get<LuaCallable*>();
			LuaValue args[] = {this, key};
			LuaValueVector results;
			func->call(args, 2, results);
			return results.empty() ? LuaValue() : std::move(results[0]);
		}
		default:
			break;
	}

	return LuaValue();
}

void LuaObject::set_item(const LuaValue& key, const LuaValue& value) {
	long long idx = -1;
	bool is_int = false;

	switch (key.index()) {
		case INDEX_INTEGER:
			idx = key.get<long long>();
			is_int = true;
			break;
		case INDEX_DOUBLE:
			is_int = is_integer_key(key.get<double>(), idx);
			break;
		default:
			break;
	}

	// 1. Array Part
	if (is_int && idx >= 1) {
		if (idx <= (long long)array_part.size()) {
			array_part[idx - 1] = value;
			if (idx == (long long)array_part.size() && value.index() == INDEX_NIL) {
				while (!array_part.empty() && array_part.back().index() == INDEX_NIL)
					array_part.pop_back();
			}
			return;
		}
		else if (value.index() != INDEX_NIL && idx < (long long)array_part.size() + 100) {
			array_part.resize(idx, LuaValue());
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
		switch (next_meta->index()) {
			case INDEX_OBJECT: {
				auto* next_obj = next_meta->get<LuaObject*>();
				next_obj->set_item(key, value);
				return;
			}
			case INDEX_FUNCTION: {
				auto* func = next_meta->get<LuaCallable*>();
				LuaValue args[] = {this, key, value};
				LuaValueVector results;
				results.clear();
				func->call(args, 3, results);
				return;
			}
			default:
				break;
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
		switch (next_meta->index()) {
			case INDEX_OBJECT: {
				auto* next_obj = next_meta->get<LuaObject*>();
				next_obj->set_item(key, value);
				return;
			}
			case INDEX_FUNCTION: {
				auto* func = next_meta->get<LuaCallable*>();
				LuaValue key_val = key; 
				LuaValue args[] = {this, key_val, value};
				LuaValueVector results;
				results.clear();
				func->call(args, 3, results);
				return;
			}
			default:
				break;
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
			if (idx == (long long)array_part.size() && value.index() == INDEX_NIL) {
				while (!array_part.empty() && array_part.back().index() == INDEX_NIL)
					array_part.pop_back();
			}
			return;
		} else if (value.index() != INDEX_NIL && idx < (long long)array_part.size() + 100) {
			if (idx > (long long)array_part.size()) array_part.resize((size_t)idx, LuaValue());
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
			if (p.first.index() == INDEX_INTEGER && p.first.get<long long>() == idx) {
				found = true; break;
			}
		}
	}

	if (!found && metatable) {
		LuaValue ni_meta = metatable->get_prop("__newindex");
		switch (ni_meta.index()) {
			case INDEX_OBJECT: {
				auto* obj = ni_meta.get<LuaObject*>();
				obj->set_item(key, value);
				return;
			}
			case INDEX_FUNCTION: {
				auto* func = ni_meta.get<LuaCallable*>();
				LuaValue args[] = {this, key, value};
				LuaValueVector results;
				results.clear();
				func->call(args, 3, results);
				return;
			}
			default:
				break;
		}
	}

	if (properties) {
		if (value.index() == INDEX_NIL) {
			properties->erase(key);
		} else {
			(*properties)[key] = value;
		}
	} else {
		for (auto it = small_props.begin(); it != small_props.end(); ++it) {
			if (it->first.index() == INDEX_INTEGER && it->first.get<long long>() == idx) {
				if (value.index() == INDEX_NIL) {
					small_props.erase(it);
				} else {
					it->second = value;
				}
				return;
			}
		}
		if (value.index() != INDEX_NIL) {
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
		size_t key_idx = key.index();
		if (key_idx == INDEX_STRING || key_idx == INDEX_STRING_VIEW) {
			uint64_t target_raw = key.raw_data();
			if (key_idx == INDEX_STRING_VIEW || (target_raw & 1ULL) == 0) {
				const std::string* pooled_ptr = intern_string_ptr(key.get<std::string_view>());
				target_raw = TAG_STRING | (reinterpret_cast<uint64_t>(pooled_ptr) | 1ULL);
			}
			for (auto& p : small_props) {
				if (p.first.raw_data() == target_raw) return p.second;
			}
			return LuaValue();
		}
		for (auto& p : small_props) {
			if (LuaValueEq{}(p.first, key)) return p.second;
		}
	}
	return LuaValue();
}

LuaValue LuaObject::get_prop(std::string_view key) {
	const std::string* pooled_ptr = intern_string_ptr(key);
	uint64_t target_raw = TAG_STRING | (reinterpret_cast<uint64_t>(pooled_ptr) | 1ULL);

	for (auto& p : small_props) {
		if (p.first.raw_data() == target_raw) return p.second;
	}

	if (properties) {
		auto it = properties->find(std::string_view(*pooled_ptr));
		if (it != properties->end()) return it->second;
	}
	return LuaValue();
}

LuaValue* LuaObject::find_prop(const LuaValue& key) {
	if (properties) {
		auto it = properties->find(key);
		if (it != properties->end()) return &it->second;
		return nullptr;
	}
	
	size_t key_idx = key.index();
	if (key_idx == INDEX_STRING || key_idx == INDEX_STRING_VIEW) {
		uint64_t target_raw = key.raw_data();
		if (key_idx == INDEX_STRING_VIEW || (target_raw & 1ULL) == 0) {
			const std::string* pooled_ptr = intern_string_ptr(key.get<std::string_view>());
			target_raw = TAG_STRING | (reinterpret_cast<uint64_t>(pooled_ptr) | 1ULL);
		}
		for (auto& p : small_props) {
			if (p.first.raw_data() == target_raw) return &p.second;
		}
		return nullptr;
	}

	for (auto& p : small_props) {
		if (LuaValueEq{}(p.first, key)) return &p.second;
	}
	return nullptr;
}

LuaValue* LuaObject::find_prop(std::string_view key) {
	const std::string* pooled_ptr = intern_string_ptr(key);
	uint64_t target_raw = TAG_STRING | (reinterpret_cast<uint64_t>(pooled_ptr) | 1ULL);

	for (auto& p : small_props) {
		if (p.first.raw_data() == target_raw) return &p.second;
	}

	if (properties) [[unlikely]] {
		auto it = properties->find(std::string_view(*pooled_ptr)); 
		if (it != properties->end()) return &it->second;
	}
	return nullptr;
}

void LuaObject::set_prop(const LuaValue& key, const LuaValue& value) {
	LuaValue interned_key = key;
	size_t key_idx = key.index();
	uint64_t target_raw = key.raw_data();
	if (key_idx == INDEX_STRING || key_idx == INDEX_STRING_VIEW) {
		if (key_idx == INDEX_STRING_VIEW || (target_raw & 1ULL) == 0) {
			const std::string* pooled_ptr = intern_string_ptr(key.get<std::string_view>());
			target_raw = TAG_STRING | (reinterpret_cast<uint64_t>(pooled_ptr) | 1ULL);
		}
		interned_key = LuaValue::from_raw(target_raw);
	}

	if (properties) {
		if (value.index() == INDEX_NIL) {
			properties->erase(interned_key);
		} else {
			(*properties)[interned_key] = value;
		}
		return;
	}

	for (auto it = small_props.begin(); it != small_props.end(); ++it) {
		if (it->first.raw_data() == target_raw) {
			if (value.index() == INDEX_NIL) {
				small_props.erase(it);
			} else {
				it->second = value;
			}
			return;
		}
	}

	if (value.index() != INDEX_NIL) {
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
	const std::string* pooled_ptr = intern_string_ptr(key);
	uint64_t target_raw = TAG_STRING | (reinterpret_cast<uint64_t>(pooled_ptr) | 1ULL);
	LuaValue key_val = LuaValue::from_raw(target_raw);

	if (properties) {
		if (value.index() == INDEX_NIL) {
			properties->erase(key_val);
		} else {
			auto it = properties->find(std::string_view(*pooled_ptr));
			if (it != properties->end()) it->second = value;
			else properties->emplace(key_val, value);
		}
		return;
	}

	for (auto it = small_props.begin(); it != small_props.end(); ++it) {
		if (it->first.raw_data() == target_raw) {
			if (value.index() == INDEX_NIL) {
				small_props.erase(it);
			} else {
				it->second = value;
			}
			return;
		}
	}

	if (value.index() != INDEX_NIL) {
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
	set_item(key, value.empty() ? LuaValue() : value[0]);
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
	if (t.index() == INDEX_OBJECT) {
		t.get<LuaObject*>()->table_insert(v);
	}
}

void lua_table_insert(const LuaValue& t, long long pos, const LuaValue& v) {
	if (t.index() == INDEX_OBJECT) {
		t.get<LuaObject*>()->table_insert(pos, v);
	}
}

// ==========================================
// Type Conversions & Printing
// ==========================================

void append_to_string(const LuaValue& value, std::string& out) {
	switch (value.index()) {
		case INDEX_NIL: out.append("nil"); break;
		case INDEX_BOOLEAN: out.append(value.get<bool>() ? "true" : "false"); break;
		case INDEX_DOUBLE: {
			char buf[64];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value.get<double>());
			out.append(buf, ptr - buf);
			break;
		}
		case INDEX_INTEGER: {
			char buf[32];
			auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value.get<long long>());
			out.append(buf, ptr - buf);
			break;
		}
		case INDEX_STRING: out.append(value.get<std::string_view>()); break;
		case INDEX_STRING_VIEW: out.append(value.get<std::string_view>()); break;
		case INDEX_OBJECT: {
			char buf[32];
			int len = snprintf(buf, sizeof(buf), "table: %p", (void*)value.get<LuaObject*>());
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
	if (value.index() == INDEX_STRING) return std::string(value.get<std::string_view>());
	std::string res;
	res.reserve(32);
	append_to_string(value, res);
	return res;
}

std::string to_cpp_string(const LuaValueVector& value) {
	return value.empty() ? "nil" : to_cpp_string(value[0]);
}

std::string get_lua_type_name(const LuaValue& val) {
	switch (val.index()) {
		case INDEX_NIL: return "nil";
		case INDEX_BOOLEAN: return "boolean";
		case INDEX_DOUBLE:
		case INDEX_INTEGER: return "number";
		case INDEX_STRING:
		case INDEX_STRING_VIEW: return "string";
		case INDEX_OBJECT: return "table";
		case INDEX_FUNCTION: return "function";
		case INDEX_CFUNCTION: return "function";
		case INDEX_COROUTINE: return "thread";
		default: return "userdata";
	}
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
	switch (a.index()) {
		case INDEX_DOUBLE:
			switch (b.index()) {
				case INDEX_DOUBLE: return a.get<double>() < b.get<double>();
				case INDEX_INTEGER: return a.get<double>() < static_cast<double>(b.get<long long>());
				default: break;
			}
			break;
		case INDEX_INTEGER:
			switch (b.index()) {
				case INDEX_INTEGER: return a.get<long long>() < b.get<long long>();
				case INDEX_DOUBLE: return static_cast<double>(a.get<long long>()) < b.get<double>();
				default: break;
			}
			break;
		case INDEX_STRING:
		case INDEX_STRING_VIEW:
			if (b.index() == INDEX_STRING || b.index() == INDEX_STRING_VIEW) {
				std::string_view sa = (a.index() == INDEX_STRING) ? std::string_view(a.get<std::string_view>()) : a.get<std::string_view>();
				std::string_view sb = (b.index() == INDEX_STRING) ? std::string_view(b.get<std::string_view>()) : b.get<std::string_view>();
				return sa < sb;
			}
			break;
		default:
			break;
	}

	// 3. Metamethod __lt
	LuaValueVector res;
	res.reserve(1);

	if (a.index() == INDEX_OBJECT) {
		auto t = a.get<LuaObject*>();
		if (t->metatable) {
			auto lt = t->metatable->get_item("__lt");
			if (lt.index() != INDEX_NIL) {
				call_lua_value(lt, res, a, b);
				return !res.empty() && is_lua_truthy(res[0]);
			}
		}
	}
	if (b.index() == INDEX_OBJECT) {
		auto t = b.get<LuaObject*>();
		if (t->metatable) {
			auto lt = t->metatable->get_item("__lt");
			if (lt.index() != INDEX_NIL) {
				call_lua_value(lt, res, a, b);
				return !res.empty() && is_lua_truthy(res[0]);
			}
		}
	}

	throw std::runtime_error("attempt to compare " + get_lua_type_name(a) + " with " + get_lua_type_name(b));
}

bool lua_less_equals(const LuaValue& a, const LuaValue& b) {
	// 1. Number vs Number
	switch (a.index()) {
		case INDEX_DOUBLE:
			switch (b.index()) {
				case INDEX_DOUBLE: return a.get<double>() <= b.get<double>();
				case INDEX_INTEGER: return a.get<double>() <= static_cast<double>(b.get<long long>());
				default: break;
			}
			break;
		case INDEX_INTEGER:
			switch (b.index()) {
				case INDEX_INTEGER: return a.get<long long>() <= b.get<long long>();
				case INDEX_DOUBLE: return static_cast<double>(a.get<long long>()) <= b.get<double>();
				default: break;
			}
			break;
		case INDEX_STRING:
		case INDEX_STRING_VIEW:
			if (b.index() == INDEX_STRING || b.index() == INDEX_STRING_VIEW) {
				std::string_view sa = (a.index() == INDEX_STRING) ? std::string_view(a.get<std::string_view>()) : a.get<std::string_view>();
				std::string_view sb = (b.index() == INDEX_STRING) ? std::string_view(b.get<std::string_view>()) : b.get<std::string_view>();
				return sa <= sb;
			}
			break;
		default:
			break;
	}

	// 3. Metamethod __le
	LuaValueVector res;
	res.reserve(1);

	if (a.index() == INDEX_OBJECT || b.index() == INDEX_OBJECT) {
		if (a.index() == INDEX_OBJECT) {
			auto t = a.get<LuaObject*>();
			if (t->metatable) {
				auto le = t->metatable->get_item("__le");
				if (le.index() != INDEX_NIL) {
					call_lua_value(le, res, a, b);
					return !res.empty() && is_lua_truthy(res[0]);
				}
			}
		}
		if (b.index() == INDEX_OBJECT) {
			auto t = b.get<LuaObject*>();
			if (t->metatable) {
				auto le = t->metatable->get_item("__le");
				if (le.index() != INDEX_NIL) {
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

// Helper to create a non-owning view of a string to avoid copy has been inlined in header

LuaValue lua_concat(const LuaValue& a, const LuaValue& b) {
	if (a.index() == INDEX_OBJECT) {
		auto* obj_a = a.get<LuaObject*>();
		if (obj_a->metatable) {
			auto concat = obj_a->metatable->get_item("__concat");
			if (concat.index() != INDEX_NIL) {
				LuaValueVector res;
				call_lua_value(concat, res, a, b);
				return res.empty() ? LuaValue() : res[0];
			}
		}
	}
	if (b.index() == INDEX_OBJECT) {
		auto* obj_b = b.get<LuaObject*>();
		if (obj_b->metatable) {
			auto concat = obj_b->metatable->get_item("__concat");
			if (concat.index() != INDEX_NIL) {
				LuaValueVector res;
				call_lua_value(concat, res, a, b);
				return res.empty() ? LuaValue() : res[0];
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
	if (a.index() == INDEX_STRING && (a.raw_data() & 1ULL) == 0) {
		auto* ls = reinterpret_cast<LuaString*>(a.raw_data() & PAYLOAD_MASK);
		if (ls && ls->get_ref_count() == 1) {
			append_to_string(b, ls->str);
			return std::move(a);
		}
	}
	return lua_concat(static_cast<const LuaValue&>(a), b);
}

LuaValue lua_concat(const LuaValue& a, LuaValue&& b) {
	if (b.index() == INDEX_STRING && (b.raw_data() & 1ULL) == 0) {
		auto* ls = reinterpret_cast<LuaString*>(b.raw_data() & PAYLOAD_MASK);
		if (ls && ls->get_ref_count() == 1) {
			std::string prefix;
			append_to_string(a, prefix);
			ls->str.insert(0, prefix);
			return std::move(b);
		}
	}
	return lua_concat(a, static_cast<const LuaValue&>(b));
}
 
LuaValue lua_concat(LuaValue&& a, LuaValue&& b) {
	if (a.index() == INDEX_STRING && (a.raw_data() & 1ULL) == 0) {
		auto* ls = reinterpret_cast<LuaString*>(a.raw_data() & PAYLOAD_MASK);
		if (ls && ls->get_ref_count() == 1) {
			append_to_string(b, ls->str);
			return std::move(a);
		}
	}
	return lua_concat(static_cast<const LuaValue&>(a), std::move(b));
}

LuaValue lua_concat_multiple(const LuaValue* args, size_t n_args) {
	bool has_object = false;
	for (size_t i = 0; i < n_args; i++) {
		if (args[i].index() == INDEX_OBJECT) { has_object = true; break; }
	}
	
	if (!has_object) {
		std::string res;
		res.reserve(n_args * 16);
		for (size_t i = 0; i < n_args; i++) {
			append_to_string(args[i], res);
		}
		return res;
	}
	
	LuaValue cur = args[n_args - 1];
	for (long long i = n_args - 2; i >= 0; i--) {
		cur = lua_concat(args[i], cur);
	}
	return cur;
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
	switch (key.index()) {
		case INDEX_INTEGER:
			idx = key.get<long long>();
			is_int = true;
			break;
		case INDEX_DOUBLE:
			is_int = is_integer_key(key.get<double>(), idx);
			break;
		default:
			break;
	}

	if (is_int && idx >= 1 && idx <= (long long)(table->array_part.size())) {
		out.assign({table->array_part[idx - 1]});
		return;
	}

	LuaValue val = table->get_prop(key);
	if (val.index() != INDEX_NIL) {
		out.assign({val});
	} else {
		out.assign({LuaValue()});
	}
}

void lua_rawset(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 3) [[unlikely]] throw std::runtime_error("bad argument #1 to 'rawset' (table expected)");
	auto table = get_object(args[0]);
	const LuaValue& key = args[1];
	const LuaValue& value = args[2];

	if (key.index() == INDEX_NIL) [[unlikely]] throw std::runtime_error("table index is nil");

	long long idx;
	bool is_int = false;
	switch (key.index()) {
		case INDEX_INTEGER:
			idx = key.get<long long>();
			is_int = true;
			break;
		case INDEX_DOUBLE:
			is_int = is_integer_key(key.get<double>(), idx);
			break;
		default:
			break;
	}

	if (is_int && idx >= 1) {
		if (idx <= (long long)table->array_part.size()) {
			table->array_part[idx - 1] = value;
			while (!table->array_part.empty() && table->array_part.back().index() == INDEX_NIL)
				table->array_part.pop_back();
			out.assign({args[0]});
			return;
		} else if (value.index() != INDEX_NIL && idx < 1000000) {
			table->array_part.resize(idx, LuaValue());
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
	switch (v.index()) {
		case INDEX_STRING:
			out.assign({static_cast<double>(v.get<std::string_view>().length())});
			break;
		case INDEX_STRING_VIEW:
			out.assign({static_cast<double>(v.get<std::string_view>().length())});
			break;
		case INDEX_OBJECT:
			out.assign({static_cast<double>(v.get<LuaObject*>()->array_part.size())});
			break;
		default:
			out.assign({0.0});
			break;
	}
}

void lua_rawequal(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 2) {
		out.assign({false});
		return;
	}

	const LuaValue& a = args[0];
	const LuaValue& b = args[1];

	// Exact type match required for rawequal
	if (a.index() != b.index()) {
		out.assign({false});
		return;
	}

	switch (a.index()) {
		case INDEX_NIL:
			out.assign({true});
			return;
		case INDEX_BOOLEAN:
			out.assign({a.get<bool>() == b.get<bool>()});
			return;
		case INDEX_DOUBLE:
			out.assign({a.get<double>() == b.get<double>()});
			return;
		case INDEX_INTEGER:
			out.assign({a.get<long long>() == b.get<long long>()});
			return;
		case INDEX_STRING:
			out.assign({a.get<std::string_view>() == b.get<std::string_view>()});
			return;
		case INDEX_STRING_VIEW:
			out.assign({a.get<std::string_view>() == b.get<std::string_view>()});
			return;
		case INDEX_OBJECT:
			out.assign({a.get<LuaObject*>() == b.get<LuaObject*>()});
			return;
		case INDEX_FUNCTION:
			out.assign({a.get<LuaCallable*>() == b.get<LuaCallable*>()});
			return;
		case INDEX_CFUNCTION:
			out.assign({a.get<LuaCFunction>().ptr == b.get<LuaCFunction>().ptr});
			return;
		case INDEX_COROUTINE:
			out.assign({a.get<LuaCoroutine*>() == b.get<LuaCoroutine*>()});
			return;
		default:
			break;
	}

	out.assign({false});
}

void lua_select(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 1) [[unlikely]] throw std::runtime_error("bad argument #1 to 'select' (value expected)");
	LuaValue index_val = args[0];
	int count = n_args - 1;

	bool is_hash = false;
	switch (index_val.index()) {
		case INDEX_STRING: if (index_val.get<std::string_view>() == "#") is_hash = true; break;
		case INDEX_STRING_VIEW: if (index_val.get<std::string_view>() == "#") is_hash = true; break;
		default: break;
	}
	if (is_hash) {
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
	auto table = args[0].get<LuaObject*>();
	LuaValue key = (n_args > 1) ? args[1] : LuaValue();

	// 1. Array Part
	long long k_int;
	bool key_is_nil = key.index() == INDEX_NIL;
	bool key_is_int = false;
	switch (key.index()) {
		case INDEX_INTEGER:
			k_int = key.get<long long>();
			key_is_int = true;
			break;
		case INDEX_DOUBLE:
			key_is_int = is_integer_key(key.get<double>(), k_int);
			break;
		default:
			break;
	}

	if (key_is_nil) {
		size_t arr_size = table->array_part.size();
		for (size_t i = 0; i < arr_size; ++i) {
			if (table->array_part[i].index() != INDEX_NIL) {
				out.assign({(long long)(i + 1), table->array_part[i]});
				return;
			}
		}
	} else if (key_is_int) {
		size_t arr_size = table->array_part.size();
		if (k_int >= 1 && k_int <= (long long)arr_size) {
			for (size_t i = (size_t)k_int; i < arr_size; ++i) {
				if (table->array_part[i].index() != INDEX_NIL) {
					out.assign({(long long)(i + 1), table->array_part[i]});
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
		if (m.index() == INDEX_FUNCTION) {
			LuaValue arg_val = table;
			m.get<LuaCallable*>()->call(&arg_val, 1, out);
			return;
		}
	}

	static const auto pairs_iter = LUA_C_FUNC(pairs_iterator);
	out.push_back(pairs_iter);
	out.push_back(args[0]);
	out.push_back(LuaValue());
}

void ipairs_iterator(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.clear();
	auto table = args[0].get<LuaObject*>();
	long long next_idx = args[1].get<long long>() + 1;

	size_t arr_size = table->array_part.size();
	if (next_idx >= 1 && next_idx <= (long long)arr_size) {
		const auto& val = table->array_part[next_idx - 1];
		if (val.index() != INDEX_NIL) {
			out.push_back(next_idx);
			out.push_back(val);
			return;
		}
	}
	out.push_back(LuaValue());
}

void lua_ipairs(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 1) [[unlikely]] throw std::runtime_error("bad argument #1 to 'ipairs' (table expected)");
	auto table = get_object(args[0]);

	out.clear();

	if (table->metatable) {
		auto m = table->metatable->get_item("__ipairs");
		if (m.index() == INDEX_FUNCTION) {
			LuaValue arg_val = table;
			m.get<LuaCallable*>()->call(&arg_val, 1, out);
			return;
		}
	}

	static const auto ipairs_iter = LUA_C_FUNC(ipairs_iterator);
	out.push_back(ipairs_iter);
	out.push_back(args[0]);
	out.push_back(0LL);
}

void lua_tonumber(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	// tonumber implementation
	LuaValue val = args[0];

	switch (val.index()) {
		case INDEX_DOUBLE:
			out.assign({val.get<double>()});
			return;
		case INDEX_INTEGER:
			out.assign({static_cast<double>(val.get<long long>())});
			return;
		case INDEX_STRING:
		case INDEX_STRING_VIEW: {
			std::string_view s = (val.index() == INDEX_STRING) ? std::string_view(val.get<std::string_view>()) : val.get<std::string_view>();
			if (s.empty()) break;

			double result;
			const char* start = s.data();
			const char* end = start + s.size();
			auto [ptr, ec] = std::from_chars(start, end, result);
			if (ec == std::errc()) {
				out.assign({result});
				return;
			}
			break;
		}
		default:
			break;
	}

	out.assign({LuaValue()});
}

// ==========================================
// Runtime Execution Helpers
// ==========================================

LuaValue LuaCallable::call0(LuaValueVector& out) {
	out.clear();
	call(nullptr, 0, out);
	return out.empty() ? LuaValue() : std::move(out[0]);
}

LuaValue LuaCallable::call1(LuaValueVector& out, const LuaValue& a1) {
	out.clear();
	const LuaValue args[] = {a1};
	call(args, 1, out);
	return out.empty() ? LuaValue() : std::move(out[0]);
}

LuaValue LuaCallable::call2(LuaValueVector& out, const LuaValue& a1, const LuaValue& a2) {
	out.clear();
	const LuaValue args[] = {a1, a2};
	call(args, 2, out);
	return out.empty() ? LuaValue() : std::move(out[0]);
}

LuaValue LuaCallable::call3(LuaValueVector& out, const LuaValue& a1, const LuaValue& a2, const LuaValue& a3) {
	out.clear();
	const LuaValue args[] = {a1, a2, a3};
	call(args, 3, out);
	return out.empty() ? LuaValue() : std::move(out[0]);
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

		switch (errh.index()) {
			case INDEX_FUNCTION:
			case INDEX_CFUNCTION: {
				LuaValue err_msg = std::string(e.what());
				LuaValueVector err_res;
				call_lua_value(errh, &err_msg, 1, err_res);
				out.push_back(err_res.empty() ? LuaValue() : err_res[0]);
				break;
			}
			default:
				out.push_back(std::string(e.what()));
				break;
		}
	}
}

void lua_assert(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args < 1) [[unlikely]] throw std::runtime_error("bad argument #1 to 'assert' (value expected)");

	if (!is_lua_truthy(args[0])) {
		LuaValue msg = (n_args > 1) ? args[1] : LuaValue(std::string_view("assertion failed!"));
		std::string err_msg = "assertion failed!";
		switch (msg.index()) {
			case INDEX_STRING: err_msg = std::string(msg.get<std::string_view>()); break;
			case INDEX_STRING_VIEW: err_msg = std::string(msg.get<std::string_view>()); break;
			default: break;
		}
		throw std::runtime_error(err_msg);
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
	out.push_back(LuaValue());
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
	out.push_back(LuaValue());
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
LuaValue lua_get_member(LuaObject* base, long long key) {
	if (!base) return LuaValue();
	return base->get_item(static_cast<double>(key));
}
