#include "table.hpp"
#include "lua_object.hpp"
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <map>

// table.unpack
std::vector<LuaValue> table_unpack(std::shared_ptr<LuaObject> args) {
    auto table = get_object(args->get("1"));
    if (!table) return {std::monostate{}};

    double i_double = std::holds_alternative<double>(args->get("2")) ? std::get<double>(args->get("2")) : 1.0;
    // Default j is #table
    double j_double;
    if (args->properties.count("3")) {
        j_double = std::get<double>(args->get("3"));
    } else {
        LuaValue len_val = lua_get_length(args->get("1"));
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
std::vector<LuaValue> table_sort(std::shared_ptr<LuaObject> args) {
    auto table = get_object(args->get("1"));
    if (!table) return {std::monostate{}};

    // Collect all integer keys from array_properties
    std::vector<std::pair<long long, LuaValue>> sortable_elements;
    for (const auto& pair : table->array_properties) {
        sortable_elements.push_back({pair.first, pair.second});
    }

    LuaValue comp_func_val = args->get("2");
    bool has_comp = std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(comp_func_val);
    std::shared_ptr<LuaFunctionWrapper> comp_func = has_comp ? std::get<std::shared_ptr<LuaFunctionWrapper>>(comp_func_val) : nullptr;

    std::sort(sortable_elements.begin(), sortable_elements.end(),
        [&](const std::pair<long long, LuaValue>& a, const std::pair<long long, LuaValue>& b) {
            if (has_comp) {
                auto comp_args = std::make_shared<LuaObject>();
                comp_args->set("1", a.second);
                comp_args->set("2", b.second);
                std::vector<LuaValue> result_vec = comp_func->func(comp_args);
                return !result_vec.empty() && is_lua_truthy(result_vec[0]);
            } else {
                return lua_less_than(a.second, b.second);
            }
        });

    // Sort 1..n for correctness with standard Lua
    LuaValue len_val = lua_get_length(args->get("1"));
    long long n = get_long_long(len_val);
    
    std::vector<LuaValue> elements;
    for (long long i = 1; i <= n; ++i) {
        elements.push_back(table->get_item(static_cast<double>(i)));
    }

    std::sort(elements.begin(), elements.end(),
        [&](const LuaValue& a, const LuaValue& b) {
            if (has_comp) {
                auto comp_args = std::make_shared<LuaObject>();
                comp_args->set("1", a);
                comp_args->set("2", b);
                std::vector<LuaValue> result_vec = comp_func->func(comp_args);
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
std::vector<LuaValue> table_pack(std::shared_ptr<LuaObject> args) {
    auto new_table = std::make_shared<LuaObject>();
    long long n = 0;
    for (int i = 1; ; ++i) {
        LuaValue val = args->get(std::to_string(i));
        // Check if argument exists (handles nil values within args)
        if (std::holds_alternative<std::monostate>(val)) {
            if (!args->properties.count(std::to_string(i)) && !args->array_properties.count(i)) {
                 break;
            }
        }
        new_table->set_item(static_cast<double>(i), val);
        n++;
    }
    new_table->set("n", static_cast<double>(n));
    return {new_table};
}

// table.move
std::vector<LuaValue> table_move(std::shared_ptr<LuaObject> args) {
    auto a1 = get_object(args->get("1"));
    if (!a1) return {std::monostate{}};

    double f_double = get_double(args->get("2"));
    double e_double = get_double(args->get("3"));
    double t_double = get_double(args->get("4"));
    
    auto a2 = get_object(args->get("5"));
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
std::vector<LuaValue> table_concat(std::shared_ptr<LuaObject> args) {
    auto table = get_object(args->get("1"));
    if (!table) return {""};

    std::string sep = std::holds_alternative<std::string>(args->get("2")) ? std::get<std::string>(args->get("2")) : "";
    double i_double = std::holds_alternative<double>(args->get("3")) ? std::get<double>(args->get("3")) : 1.0;
    
    double j_double;
    if (args->properties.count("4") || args->array_properties.count(4)) {
         j_double = get_double(args->get("4"));
    } else {
         LuaValue len_val = lua_get_length(args->get("1"));
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
std::vector<LuaValue> table_insert(std::shared_ptr<LuaObject> args) {
    auto table = get_object(args->get("1"));
    if (table) {
        // Check number of arguments to decide overload
        // args has "1", "2", "3"...
        bool has_pos = false;
        if (args->properties.count("3") || args->array_properties.count(3)) {
            has_pos = true;
        }

        if (has_pos) {
            // insert(table, pos, value)
            long long pos = get_long_long(args->get("2"));
            LuaValue val = args->get("3");
            
            LuaValue len_val = lua_get_length(args->get("1"));
            long long len = get_long_long(len_val);

            // Shift elements up
            for (long long i = len; i >= pos; --i) {
                table->set_item(static_cast<double>(i + 1), table->get_item(static_cast<double>(i)));
            }
            table->set_item(static_cast<double>(pos), val);
        } else {
            // insert(table, value) -> append
            LuaValue val = args->get("2");
            LuaValue len_val = lua_get_length(args->get("1"));
            long long len = get_long_long(len_val);
            table->set_item(static_cast<double>(len + 1), val);
        }
    }
    return {std::monostate{}};
}

// table.remove
std::vector<LuaValue> table_remove(std::shared_ptr<LuaObject> args) {
    auto table = get_object(args->get("1"));
    if (table) {
        LuaValue len_val = lua_get_length(args->get("1"));
        long long len = get_long_long(len_val);
        
        long long pos = len;
        if (args->properties.count("2") || args->array_properties.count(2)) {
            pos = get_long_long(args->get("2"));
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
    auto table_lib = std::make_shared<LuaObject>();

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
