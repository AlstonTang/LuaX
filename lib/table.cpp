#include "table.hpp"
#include "lua_object.hpp"
#include <vector>
#include <algorithm>
#include <string>
#include <map>

// table.unpack
void table_unpack(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	auto table = get_object(args[0]);
	if (!table) {
		out.assign({std::monostate{}});
		return;
	};

	double i_double = n_args >= 2 && std::holds_alternative<double>(args[1]) ? std::get<double>(args[1]) : 1.0;
	// Default j is #table
	double j_double;
	if (n_args >= 3) {
		j_double = std::get<double>(args[2]);
	}
	else {
		LuaValue len_val = lua_get_length(args[0]);
		j_double = get_double(len_val);
	}

	long long i = static_cast<long long>(i_double);
	long long j = static_cast<long long>(j_double);

	out.clear();
	for (long long k = i; k <= j; ++k) {
		out.push_back(table->get_item(static_cast<double>(k)));
	}
}

// table.sort
void table_sort(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	auto table = get_object(args[0]);
	if (!table) {
		out.assign({std::monostate{}});
		return;
	}

	LuaValue comp_func_val = (n_args >= 2) ? args[1] : LuaValue(std::monostate{});
	bool has_comp = std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(comp_func_val);
	std::shared_ptr<LuaFunctionWrapper> comp_func = has_comp
		                                                ? std::get<std::shared_ptr<LuaFunctionWrapper>>(comp_func_val)
		                                                : nullptr;
	std::vector<LuaValue> comp_buffer;
	comp_buffer.reserve(1);

	// 1. Standard Lua table.sort behavior: Sort items 1 to N
	LuaValue len_val = lua_get_length(args[0]);
	long long n = get_long_long(len_val);

	if (n <= 1) {
		out.assign({std::monostate{}});
		return;
	}

	// Extract elements
	std::vector<LuaValue> elements;
	elements.reserve(n);
	for (long long i = 1; i <= n; ++i) {
		elements.push_back(table->get_item(static_cast<double>(i)));
	}

	// Sort
	std::sort(elements.begin(), elements.end(),
	          [&](const LuaValue& a, const LuaValue& b) {
		          if (has_comp) {
			          // 1. Reset buffer
			          comp_buffer.clear();

			          // 2. Call Lua function with Output Parameter
			          LuaValue func_args[] = {a, b};
			          comp_func->func(func_args, 2, comp_buffer);

			          // 3. Check Result
			          if (comp_buffer.empty()) return false; // Default false if nil returned
			          return is_lua_truthy(comp_buffer[0]);
		          }
		          else {
			          // Default Lua comparison (<)
			          return lua_less_than(a, b);
		          }
	          });

	// Write back
	for (long long i = 0; i < n; ++i) {
		table->set_item(static_cast<double>(i + 1), elements[i]);
	}

	out.clear();
}

// table.pack
void table_pack(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
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
	out.assign({new_table});
	return;
}

// table.move
void table_move(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	auto a1 = get_object(args[0]);
	if (!a1) {
		out.assign({std::monostate{}});
		return;
	};

	double f_double = get_double(args[1]);
	double e_double = get_double(args[2]);
	double t_double = get_double(args[3]);

	auto a2 = (n_args >= 5) ? get_object(args[4]) : nullptr;
	if (!a2) a2 = a1;

	long long f = static_cast<long long>(f_double);
	long long e = static_cast<long long>(e_double);
	long long t = static_cast<long long>(t_double);

	if (f > e) {
		out.assign({a2});
		return;
	};

	if (t <= f || t > e) {
		for (long long idx = f; idx <= e; ++idx) {
			a2->set_item(static_cast<double>(t + (idx - f)), a1->get_item(static_cast<double>(idx)));
		}
	}
	else {
		for (long long idx = e; idx >= f; --idx) {
			a2->set_item(static_cast<double>(t + (idx - f)), a1->get_item(static_cast<double>(idx)));
		}
	}
	out.assign({a2});
	return;
}

// table.concat
void table_concat(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	auto table = get_object(args[0]);
	if (!table) {
		out.assign({""});
		return;
	};

	std::string sep = n_args >= 2 && std::holds_alternative<std::string>(args[1]) ? std::get<std::string>(args[1]) : "";
	double i_double = n_args >= 3 && std::holds_alternative<double>(args[2]) ? std::get<double>(args[2]) : 1.0;

	double j_double;
	if (n_args >= 4) {
		j_double = get_double(args[3]);
	}
	else {
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
	out.assign({result});
	return;
}

// table.insert
void table_insert(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
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
		}
		else {
			// insert(table, value) -> append
			LuaValue val = args[1];
			LuaValue len_val = lua_get_length(args[0]);
			long long len = get_long_long(len_val);
			table->set_item(static_cast<double>(len + 1), val);
		}
	}
	out.assign({std::monostate{}});
	return;
}

// table.remove
void table_remove(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (auto table = get_object(args[0])) {
		LuaValue len_val = lua_get_length(args[0]);
		long long len = get_long_long(len_val);

		long long pos = len;
		if (n_args >= 2) {
			pos = get_long_long(args[1]);
		}

		if (pos > len || pos < 1) {
			out.assign({std::monostate{}});
			return;
		};

		LuaValue removed_val = table->get_item(static_cast<double>(pos));

		for (long long i = pos; i < len; ++i) {
			table->set_item(static_cast<double>(i), table->get_item(static_cast<double>(i + 1)));
		}
		table->set_item(static_cast<double>(len), std::monostate{}); // Remove last

		out.assign({removed_val});
		return;
	}
	out.assign({std::monostate{}});
	return;
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
