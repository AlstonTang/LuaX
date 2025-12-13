#include "table.hpp"
#include "lua_object.hpp"
#include <vector>
#include <algorithm>
#include <string>
#include <map>

// table.unpack
std::vector<LuaValue> table_unpack(const LuaValue* args, size_t n_args) {
	auto table = get_object(args[0]);
	if (!table) return {std::monostate{}};

	double i_double = n_args >= 2 && std::holds_alternative<double>(args[1]) ? std::get<double>(args[1]) : 1.0;
	// Default j is #table
	double j_double;
	if (n_args >= 3) {
		j_double = std::get<double>(args[2]);
	} else {
		LuaValue len_val = lua_get_length(args[0]);
		j_double = get_double(len_val);
	}

	long long i = static_cast<long long>(i_double);
	long long j = static_cast<long long>(j_double);

	std::vector<LuaValue> unpacked_values_vec;
	for (long long k = i; k <= j; ++k) {
		unpacked_values_vec.push_back(table->get_item(static_cast<double>(k)));
	}
	return unpacked_values_vec;
}

// table.sort
std::vector<LuaValue> table_sort(const LuaValue* args, size_t n_args) {
	auto table = get_object(args[0]);
	if (!table) return {std::monostate{}};

	// Collect all integer keys from array_properties
	std::vector<std::pair<long long, LuaValue>> sortable_elements;
	for (const auto& pair : table->array_properties) {
		sortable_elements.push_back({pair.first, pair.second});
	}

	LuaValue comp_func_val = (n_args >= 2) ? args[1] : LuaValue(std::monostate{});
	bool has_comp = std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(comp_func_val);
	std::shared_ptr<LuaFunctionWrapper> comp_func = has_comp ? std::get<std::shared_ptr<LuaFunctionWrapper>>(comp_func_val) : nullptr;

	std::sort(sortable_elements.begin(), sortable_elements.end(),
		[&](const std::pair<long long, LuaValue>& a, const std::pair<long long, LuaValue>& b) {
			if (has_comp) {
				LuaValue func_args[] = {a.second, b.second};
				std::vector<LuaValue> result_vec = comp_func->func(func_args, 2);
				return !result_vec.empty() && is_lua_truthy(result_vec[0]);
			} else {
				return lua_less_than(a.second, b.second);
			}
		});

	// Sort 1...n for correctness with standard Lua
	LuaValue len_val = lua_get_length(args[0]);
	long long n = get_long_long(len_val);
	
	std::vector<LuaValue> elements;
	for (long long i = 1; i <= n; ++i) {
		elements.push_back(table->get_item(static_cast<double>(i)));
	}

	std::sort(elements.begin(), elements.end(),
		[&](const LuaValue& a, const LuaValue& b) {
			if (has_comp) {
				LuaValue func_args[] = {a, b};
				std::vector<LuaValue> result_vec = comp_func->func(func_args, 2);
				return !result_vec.empty() && is_lua_truthy(result_vec[0]);
			} else {
				return lua_less_than(a, b);
			}
		});

	for (long long i = 0; i < n; ++i) {
		table->set_item(static_cast<double>(i + 1), elements[i]);
	}

	return {std::monostate{}};
}

// table.pack
std::vector<LuaValue> table_pack(const LuaValue* args, size_t n_args) {
	auto new_table = std::make_shared<LuaObject>();
	long long n = 0;
	for (size_t i = 0; i < n_args; ++i) {
		LuaValue val = args[i];
		if (std::holds_alternative<std::monostate>(val)) {
			continue;
		}
		new_table->set_item(static_cast<double>(i + 1), val);
		++n;
	}
	new_table->set("n", static_cast<double>(n_args));
	return {new_table};
}

// table.move
std::vector<LuaValue> table_move(const LuaValue* args, size_t n_args) {
	auto a1 = get_object(args[0]);
	if (!a1) return {std::monostate{}};

	double f_double = get_double(args[1]);
	double e_double = get_double(args[2]);
	double t_double = get_double(args[3]);
	
	auto a2 = (n_args >= 5) ? get_object(args[4]) : nullptr;
	if (!a2) a2 = a1; 

	long long f = static_cast<long long>(f_double);
	long long e = static_cast<long long>(e_double);
	long long t = static_cast<long long>(t_double);

	if (f > e) return {a2}; 

	if (t <= f || t > e) { 
		for (long long idx = f; idx <= e; ++idx) {
			a2->set_item(static_cast<double>(t + (idx - f)), a1->get_item(static_cast<double>(idx)));
		}
	} else { 
		for (long long idx = e; idx >= f; --idx) {
			a2->set_item(static_cast<double>(t + (idx - f)), a1->get_item(static_cast<double>(idx)));
		}
	}
	return {a2};
}

// table.concat
std::vector<LuaValue> table_concat(const LuaValue* args, size_t n_args) {
	auto table = get_object(args[0]);
	if (!table) return {""};

	std::string sep = n_args >= 2 && std::holds_alternative<std::string>(args[1]) ? std::get<std::string>(args[1]) : "";
	double i_double = n_args >= 3 && std::holds_alternative<double>(args[2]) ? std::get<double>(args[2]) : 1.0;
	
	double j_double;
	if (n_args >= 4) {
		 j_double = get_double(args[3]);
	} else {
		 LuaValue len_val = lua_get_length(args[0]);
		 j_double = get_double(len_val);
	}

	long long i = static_cast<long long>(i_double);
	long long j = static_cast<long long>(j_double);

	std::string result = "";
	bool first = true;
	for (long long k = i; k <= j; ++k) {
		LuaValue val = table->get_item(static_cast<double>(k));
		if (!std::holds_alternative<std::monostate>(val)) {
			if (!first) {
				result += sep;
			}
			result += to_cpp_string(val);
			first = false;
		}
	}
	return {result};
}

// table.insert
std::vector<LuaValue> table_insert(const LuaValue* args, size_t n_args) {
	if (auto table = get_object(args[0])) {
		// Check number of arguments to decide overload
		// args has "1", "2", "3"...
		bool has_pos = false;
		if (n_args >= 3) {
			has_pos = true;
		}

		if (has_pos) {
			// insert(table, pos, value)
			long long pos = get_long_long(args[1]);
			LuaValue val = args[2];
			
			LuaValue len_val = lua_get_length(args[0]);
			long long len = get_long_long(len_val);

			// Shift elements up
			for (long long i = len; i >= pos; --i) {
				table->set_item(static_cast<double>(i + 1), table->get_item(static_cast<double>(i)));
			}
			table->set_item(static_cast<double>(pos), val);
		} else {
			// insert(table, value) -> append
			LuaValue val = args[1];
			LuaValue len_val = lua_get_length(args[0]);
			long long len = get_long_long(len_val);
			table->set_item(static_cast<double>(len + 1), val);
		}
	}
	return {std::monostate{}};
}

// table.remove
std::vector<LuaValue> table_remove(const LuaValue* args, size_t n_args) {
	if (auto table = get_object(args[0])) {
		LuaValue len_val = lua_get_length(args[0]);
		long long len = get_long_long(len_val);
		
		long long pos = len;
		if (n_args >= 2) {
			pos = get_long_long(args[1]);
		}

		if (pos > len || pos < 1) return {std::monostate{}};

		LuaValue removed_val = table->get_item(static_cast<double>(pos));

		for (long long i = pos; i < len; ++i) {
			table->set_item(static_cast<double>(i), table->get_item(static_cast<double>(i + 1)));
		}
		table->set_item(static_cast<double>(len), std::monostate{}); // Remove last
		
		return {removed_val};
	}
	return {std::monostate{}};
}

std::shared_ptr<LuaObject> create_table_library() {
	static std::shared_ptr<LuaObject> table_lib;
	if (table_lib) return table_lib;

	table_lib = std::make_shared<LuaObject>();

	table_lib->properties = {
		{"concat", std::make_shared<LuaFunctionWrapper>(table_concat)},
		{"insert", std::make_shared<LuaFunctionWrapper>(table_insert)},
		{"move", std::make_shared<LuaFunctionWrapper>(table_move)},
		{"pack", std::make_shared<LuaFunctionWrapper>(table_pack)},
		{"remove", std::make_shared<LuaFunctionWrapper>(table_remove)},
		{"sort", std::make_shared<LuaFunctionWrapper>(table_sort)},
		{"unpack", std::make_shared<LuaFunctionWrapper>(table_unpack)}
	};

	return table_lib;
}
