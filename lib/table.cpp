#include "table.hpp"
#include "lua_object.hpp"
#include <vector>
#include <algorithm>

// table.insert
LuaValue table_insert(std::shared_ptr<LuaObject> args) {
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
    return std::monostate{};
}

// table.remove
LuaValue table_remove(std::shared_ptr<LuaObject> args) {
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
    return std::monostate{};
}


std::shared_ptr<LuaObject> create_table_library() {
    auto table_lib = std::make_shared<LuaObject>();

    table_lib->set("insert", std::make_shared<LuaFunctionWrapper>(table_insert));
    table_lib->set("remove", std::make_shared<LuaFunctionWrapper>(table_remove));

    return table_lib;
}
