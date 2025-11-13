#include "table.hpp"
#include "lua_object.hpp"
#include <vector>
#include <algorithm>
#include <stdexcept> // Added for std::invalid_argument, std::out_of_range
#include <string> // For std::stoll

// table.concat
std::vector<LuaValue> table_unpack(std::shared_ptr<LuaObject> args) {
    auto table = get_object(args->get("1"));
    if (!table) return {std::monostate{}};

    double i_double = std::holds_alternative<double>(args->get("2")) ? std::get<double>(args->get("2")) : 1.0;
    double j_double = std::holds_alternative<double>(args->get("3")) ? std::get<double>(args->get("3")) : static_cast<double>(table->properties.size());

    long long i = static_cast<long long>(i_double);
    long long j = static_cast<long long>(j_double);

    std::vector<LuaValue> unpacked_values_vec;
    for (long long k = i; k <= j; ++k) {
        if (table->properties.count(std::to_string(k))) {
            unpacked_values_vec.push_back(table->properties[std::to_string(k)]);
        } else {
            unpacked_values_vec.push_back(std::monostate{}); // nil for missing values
        }
    }
    return unpacked_values_vec;
}

// table.sort
std::vector<LuaValue> table_sort(std::shared_ptr<LuaObject> args) {
    auto table = get_object(args->get("1"));
    if (!table) return {std::monostate{}};

    std::vector<std::pair<long long, LuaValue>> sortable_elements;
    for (const auto& pair : table->properties) {
        try {
            long long key_num = std::stoll(pair.first);
            sortable_elements.push_back({key_num, pair.second});
        } catch (const std::invalid_argument& e) {
            // Ignore non-numeric keys for sorting
        } catch (const std::out_of_range& e) {
            // Ignore out of range numeric keys
        }
    }

    LuaValue comp_func_val = args->get("2");
    if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(comp_func_val)) {
        auto comp_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(comp_func_val);
        std::sort(sortable_elements.begin(), sortable_elements.end(),
            [&](const std::pair<long long, LuaValue>& a, const std::pair<long long, LuaValue>& b) {
                auto comp_args = std::make_shared<LuaObject>();
                comp_args->set("1", a.second);
                comp_args->set("2", b.second);
                std::vector<LuaValue> result_vec = comp_func->func(comp_args);
                return !result_vec.empty() && std::holds_alternative<bool>(result_vec[0]) && std::get<bool>(result_vec[0]);
            });
    } else {
        std::sort(sortable_elements.begin(), sortable_elements.end(),
            [](const std::pair<long long, LuaValue>& a, const std::pair<long long, LuaValue>& b) {
                // Default comparison: less than
                if (std::holds_alternative<double>(a.second) && std::holds_alternative<double>(b.second)) {
                    return std::get<double>(a.second) < std::get<double>(b.second);
                }
                if (std::holds_alternative<std::string>(a.second) && std::holds_alternative<std::string>(b.second)) {
                    return std::get<std::string>(a.second) < std::get<std::string>(b.second);
                }
                // Fallback for other types, might need more robust Lua-like comparison
                return false;
            });
    }

    // Re-insert sorted elements into the table
    table->properties.clear(); // Clear existing numeric keys
    for (size_t i = 0; i < sortable_elements.size(); ++i) {
        table->set(std::to_string(i + 1), sortable_elements[i].second);
    }

    return {std::monostate{}};
}

// table.pack
std::vector<LuaValue> table_pack(std::shared_ptr<LuaObject> args) {
    auto new_table = std::make_shared<LuaObject>();
    long long n = 0;
    for (int i = 1; ; ++i) {
        LuaValue val = args->get(std::to_string(i));
        if (std::holds_alternative<std::monostate>(val)) {
            // Check if the next argument is also nil, to handle actual nil values vs end of args
            LuaValue next_val = args->get(std::to_string(i + 1));
            if (std::holds_alternative<std::monostate>(next_val)) {
                break;
            }
        }
        new_table->set(std::to_string(i), val);
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
    if (!a2) a2 = a1; // Default to a1 if a2 is not provided

    long long f = static_cast<long long>(f_double);
    long long e = static_cast<long long>(e_double);
    long long t = static_cast<long long>(t_double);

    if (f > e) return {a2}; // Nothing to move

    // Handle overlapping moves correctly (iterate forwards or backwards)
    if (t <= f || t > e) { // Non-overlapping or moving to the right
        for (long long idx = f; idx <= e; ++idx) {
            a2->set(std::to_string(t + (idx - f)), a1->get(std::to_string(idx)));
        }
    } else { // Overlapping and moving to the left
        for (long long idx = e; idx >= f; --idx) {
            a2->set(std::to_string(t + (idx - f)), a1->get(std::to_string(idx)));
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
    double j_double = std::holds_alternative<double>(args->get("4")) ? std::get<double>(args->get("4")) : static_cast<double>(table->properties.size());

    long long i = static_cast<long long>(i_double);
    long long j = static_cast<long long>(j_double);

    std::string result = "";
    bool first = true;
    for (long long k = i; k <= j; ++k) {
        if (table->properties.count(std::to_string(k))) {
            if (!first) {
                result += sep;
            }
            result += to_cpp_string(table->properties[std::to_string(k)]);
            first = false;
        }
    }
    return {result};
}

// table.insert
std::vector<LuaValue> table_insert(std::shared_ptr<LuaObject> args) {
    auto table = get_object(args->get("1"));
    if (table) {
        if (args->properties.count("3")) {
            // insert at position
            int pos = static_cast<int>(get_double(args->get("2")));
            LuaValue val = args->get("3");
            table->properties[std::to_string(pos)] = val;
        } else {
            // insert at the end
            LuaValue val = args->get("2");
            int next_index = table->properties.size() + 1;
            table->properties[std::to_string(next_index)] = val;
        }
    }
    return {std::monostate{}};
}

// table.remove
std::vector<LuaValue> table_remove(std::shared_ptr<LuaObject> args) {
    auto table = get_object(args->get("1"));
    if (table) {
        int pos = table->properties.size();
        if (args->properties.count("2")) {
            pos = static_cast<int>(get_double(args->get("2")));
        }

        for (int i = pos; i < table->properties.size(); ++i) {
            table->properties[std::to_string(i)] = table->properties[std::to_string(i + 1)];
        }
        table->properties.erase(std::to_string(table->properties.size()));
    }
    return {std::monostate{}};
}


std::shared_ptr<LuaObject> create_table_library() {
    auto table_lib = std::make_shared<LuaObject>();

    table_lib->set("concat", std::make_shared<LuaFunctionWrapper>(table_concat));
    table_lib->set("insert", std::make_shared<LuaFunctionWrapper>(table_insert));
    table_lib->set("move", std::make_shared<LuaFunctionWrapper>(table_move));
    table_lib->set("sort", std::make_shared<LuaFunctionWrapper>(table_sort));
    table_lib->set("unpack", std::make_shared<LuaFunctionWrapper>(table_unpack));
    table_lib->set("remove", std::make_shared<LuaFunctionWrapper>(table_remove));

    return table_lib;
}
