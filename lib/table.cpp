#include "table.hpp"
#include "lua_object.hpp"
#include <vector>
#include <algorithm>
#include <string>

// table.unpack(list [, i [, j]])
void table_unpack(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	auto table = get_object(args[0]);
	if (!table) {
		out.assign({std::monostate{}});
		return;
	}

	// Default i = 1, j = #list
	long long i = (n_args >= 2) ? get_long_long(args[1]) : 1;
	long long j;
	if (n_args >= 3) {
		j = get_long_long(args[2]);
	}
	else {
		j = static_cast<long long>(table->array_part.size());
	}

	out.clear();
	if (i > j) return;

	// Optimization: If the range is within our current vector, use push_back directly
	for (long long k = i; k <= j; ++k) {
		if (k >= 1 && k <= static_cast<long long>(table->array_part.size())) {
			out.push_back(table->array_part[k - 1]);
		}
		else {
			// Fallback for indices outside the current vector bounds (sparse elements)
			out.push_back(table->get_item(static_cast<double>(k)));
		}
	}
}

// table.sort(list [, comp])
void table_sort(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	auto table = get_object(args[0]);
	if (!table || table->array_part.empty()) {
		out.assign({std::monostate{}});
		return;
	}

	LuaValue comp_func_val = (n_args >= 2) ? args[1] : LuaValue(std::monostate{});
	bool has_comp = std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(comp_func_val);
	auto comp_func = has_comp ? std::get<std::shared_ptr<LuaFunctionWrapper>>(comp_func_val) : nullptr;

	// table.sort usually only sorts the array part (1 to #list)
	// Since we are using std::vector, we sort the array_part directly.
	std::sort(table->array_part.begin(), table->array_part.end(),
	          [&](const LuaValue& a, const LuaValue& b) {
		          if (has_comp) {
			          thread_local std::vector<LuaValue> comp_buffer;
			          comp_buffer.clear();
			          LuaValue func_args[] = {a, b};
			          comp_func->func(func_args, 2, comp_buffer);
			          return !comp_buffer.empty() && is_lua_truthy(comp_buffer[0]);
		          }
		          return lua_less_than(a, b);
	          });

	out.clear();
}

// table.pack(...)
void table_pack(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	auto new_table = std::make_shared<LuaObject>();

	// Efficiently populate the vector
	new_table->array_part.reserve(n_args);
	for (size_t i = 0; i < n_args; ++i) {
		new_table->array_part.push_back(args[i]);
	}

	// Lua's table.pack also sets the "n" field to the number of arguments
	new_table->set("n", static_cast<double>(n_args));

	out.assign({new_table});
}

// table.move(a1, f, e, t [, a2])
void table_move(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	auto a1 = get_object(args[0]);
	if (!a1) return;

	long long f = get_long_long(args[1]);
	long long e = get_long_long(args[2]);
	long long t = get_long_long(args[3]);
	auto a2 = (n_args >= 5) ? get_object(args[4]) : a1;

	if (f > e) {
		out.assign({a2});
		return;
	}

	// To handle overlapping moves (like moving 1..3 to 2..4), we collect first
	std::vector<LuaValue> range;
	range.reserve(static_cast<size_t>(e - f + 1));
	for (long long i = f; i <= e; ++i) {
		range.push_back(a1->get_item(static_cast<double>(i)));
	}

	// Write back to a2
	for (size_t i = 0; i < range.size(); ++i) {
		a2->set_item(static_cast<double>(t + i), range[i]);
	}

	out.assign({a2});
}

// table.concat(list [, sep [, i [, j]]])
void table_concat(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	auto table = get_object(args[0]);
	if (!table) return;

	std::string sep = (n_args >= 2) ? to_cpp_string(args[1]) : "";
	long long i = (n_args >= 3) ? get_long_long(args[2]) : 1;
	long long j = (n_args >= 4) ? get_long_long(args[3]) : static_cast<long long>(table->array_part.size());

	std::string result;
	for (long long k = i; k <= j; ++k) {
		LuaValue val = (k >= 1 && k <= (long long)table->array_part.size())
			               ? table->array_part[k - 1]
			               : table->get_item(static_cast<double>(k));

		if (std::holds_alternative<std::monostate>(val)) {
			throw std::runtime_error("invalid value (nil) at index " + std::to_string(k) + " in table.concat");
		}

		if (k > i) result += sep;
		result += to_cpp_string(val);
	}
	out.assign({result});
}

// table.insert([list,] [pos,] value)
void table_insert(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	auto table = get_object(args[0]);
	if (!table) return;

	if (n_args == 2) {
		// Overload: table.insert(table, value) -> append
		table->array_part.push_back(args[1]);
	}
	else if (n_args >= 3) {
		// Overload: table.insert(table, pos, value)
		long long pos = get_long_long(args[1]);
		const LuaValue& val = args[2];

		if (pos >= 1 && pos <= static_cast<long long>(table->array_part.size() + 1)) {
			table->array_part.insert(table->array_part.begin() + (pos - 1), val);
		}
		else {
			// Fallback for sparse insertion
			table->set_item(args[1], val);
		}
	}
	out.assign({std::monostate{}});
}

// table.remove(list [, pos])
void table_remove(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	auto table = get_object(args[0]);
	if (!table || table->array_part.empty()) {
		out.assign({std::monostate{}});
		return;
	}

	long long len = static_cast<long long>(table->array_part.size());
	long long pos = (n_args >= 2) ? get_long_long(args[1]) : len;

	if (pos < 1 || pos > len) {
		out.assign({std::monostate{}});
		return;
	}

	LuaValue removed_val = table->array_part[pos - 1];
	table->array_part.erase(table->array_part.begin() + (pos - 1));

	out.assign({removed_val});
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
