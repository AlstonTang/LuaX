#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <variant>
#include <regex>
#include <functional>
#include "lua_object.hpp"

#include "other_module.hpp"
#include "math.hpp"
#include "string.hpp"
#include "table.hpp"
#include "os.hpp"
#include "io.hpp"
#include "package.hpp"
int main() {
    _G->set("math", create_math_library());
    _G->set("string", create_string_library());
    _G->set("table", create_table_library());
    _G->set("os", create_os_library());
    _G->set("io", create_io_library());
    _G->set("package", create_package_library());
    _G->set("_VERSION", LuaValue(std::string("Lua 5.4")));
    _G->set("tonumber", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {
        // tonumber implementation
        LuaValue val = args->get("1");
        if (std::holds_alternative<double>(val)) {
            return val;
        } else if (std::holds_alternative<std::string>(val)) {
            std::string s = std::get<std::string>(val);
            try {
                // Check if the string contains only digits and an optional decimal point
                if (s.find_first_not_of("0123456789.") == std::string::npos) {
                    return std::stod(s);
                }
            } catch (...) {
                // Fall through to return nil
            }
        }
        return std::monostate{}; // nil
    }));
    _G->set("tostring", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {
        // tostring implementation
        return to_cpp_string(args->get("1"));
    }));
    _G->set("type", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {
        // type implementation
        LuaValue val = args->get("1");
        if (std::holds_alternative<std::monostate>(val)) return "nil";
        if (std::holds_alternative<bool>(val)) return "boolean";
        if (std::holds_alternative<double>(val)) return "number";
        if (std::holds_alternative<std::string>(val)) return "string";
        if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) return "table";
        if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(val)) return "function";
        return "unknown";
    }));
    _G->set("getmetatable", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {
        // getmetatable implementation
        LuaValue val = args->get("1");
        if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) {
            auto obj = std::get<std::shared_ptr<LuaObject>>(val);
            if (obj->metatable) {
                return obj->metatable;
            }
        }
        return std::monostate{}; // nil
    }));
    _G->set("error", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {
        // error implementation
        LuaValue message = args->get("1");
        try {
            throw std::runtime_error(to_cpp_string(message));
        } catch (const std::exception& e) {
            std::cerr << "Error function caught exception: " << e.what() << std::endl;
            throw;
        }
        return std::monostate{}; // Should not be reached
    }));
    _G->set("pcall", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {
        // pcall implementation
        LuaValue func_to_call = args->get("1");
        if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(func_to_call)) {
            auto callable_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(func_to_call);
            auto func_args = std::make_shared<LuaObject>();
            for (int i = 2; ; ++i) {
                LuaValue arg = args->get(std::to_string(i));
                if (std::holds_alternative<std::monostate>(arg)) break;
                func_args->set(std::to_string(i - 1), arg);
            }
            try {
                LuaValue result = callable_func->func(func_args);
                auto results = std::make_shared<LuaObject>();
                results->set("1", true);
                results->set("2", result);
                return results;
            } catch (...) {
                auto results = std::make_shared<LuaObject>();
                results->set("1", false);
                results->set("2", "An unknown error occurred");
                return results;
            }
        }
        return false; // Not a callable function
    }));
LuaValue a = LuaValue(10.0);
LuaValue b = LuaValue(20.0);
LuaValue c = (get_double(a) + get_double(b));
print_value("Sum:");std::cout << "\t";print_value(c);std::cout << std::endl;;
std::string name = "World";
print_value("Hello,");std::cout << "\t";print_value(name);std::cout << std::endl;;
if ((get_double(a) > get_double(b))) {
    print_value("a is greater than b");std::cout << std::endl;;
} else if ((get_double(a) < get_double(b))) {
    print_value("a is less than b");std::cout << std::endl;;
} else {
    print_value("a is equal to b");std::cout << std::endl;;
}

for (LuaValue i = LuaValue(1.0); get_double(i) <= get_double(LuaValue(3.0)); i = LuaValue(get_double(i) + get_double(LuaValue(1.0)))) {
    print_value("For loop iteration:");std::cout << "\t";print_value(i);std::cout << std::endl;;
}
LuaValue count = LuaValue(0.0);
while ((get_double(count) < get_double(LuaValue(2.0)))) {
    print_value("While loop iteration:");std::cout << "\t";print_value(count);std::cout << std::endl;;
    count = (get_double(count) + get_double(LuaValue(1.0)));
}
std::string text = "hello world";
std::string pattern = "world";
if (std::regex_search(text, std::regex(pattern))) {
    print_value("Pattern found!");std::cout << std::endl;;
}

LuaValue pos = LuaValue(text.find("world") != std::string::npos ? static_cast<double>(text.find("world") + 1) : 0.0);
print_value("Pattern 'world' found at position:");std::cout << "\t";print_value(pos);std::cout << std::endl;;
LuaValue new_text = std::regex_replace(text, std::regex("world"), "lua");
print_value("String gsub:");std::cout << "\t";print_value(new_text);std::cout << std::endl;;
LuaValue other = other_module::load();
print_value("Module name:");std::cout << "\t";print_value(get_object(other)->get("name"));std::cout << std::endl;;
print_value("Module version:");std::cout << "\t";print_value(get_object(other)->get("version"));std::cout << std::endl;;
LuaValue greeting = std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(other)->get("greet"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", "Gemini");
return temp_args; } )());
print_value(greeting);std::cout << std::endl;;
std::shared_ptr<LuaObject> defaults = ( [&]() { auto temp_table_7831 = std::make_shared<LuaObject>();
temp_table_7831->set("x", LuaValue(0.0));
temp_table_7831->set("y", LuaValue(0.0));
temp_table_7831->set("color", "blue");
 return temp_table_7831; } )();
std::shared_ptr<LuaObject> mt = ( [&]() { auto temp_table_7985 = std::make_shared<LuaObject>();
 return temp_table_7985; } )();
get_object(LuaValue(mt))->set("__index", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {
    LuaValue table = args->get("1");
    LuaValue key = args->get("2");
    print_value("Accessing missing key:");std::cout << "\t";print_value(key);std::cout << std::endl;;
    return defaults;
    key;
}));
get_object(LuaValue(mt))->set("__newindex", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {
    LuaValue table = args->get("1");
    LuaValue key = args->get("2");
    LuaValue value = args->get("3");
    print_value("Attempting to set new key:");std::cout << "\t";print_value(key);std::cout << "\t";print_value("with value:");std::cout << "\t";print_value(value);std::cout << std::endl;;
    if (lua_equals(key, "z")) {
    get_object(table)->properties[std::get<std::string>(key)] = value;;
} else {
    print_value(to_cpp_string(to_cpp_string("Cannot set key '") + to_cpp_string(key)) + to_cpp_string("'. Use rawset if intended."));std::cout << std::endl;;
}

return std::monostate{};
}));
std::shared_ptr<LuaObject> my_object = ( [&]() { auto temp_table_9117 = std::make_shared<LuaObject>();
 return temp_table_9117; } )();
get_object(my_object)->set_metatable(get_object(mt));;
print_value("my_object.x:");std::cout << "\t";print_value(get_object(my_object)->get("x"));std::cout << std::endl;;
print_value("my_object.color:");std::cout << "\t";print_value(get_object(my_object)->get("color"));std::cout << std::endl;;
get_object(LuaValue(my_object))->set("a", LuaValue(100.0));
get_object(LuaValue(my_object))->set("z", LuaValue(50.0));
print_value("my_object.z:");std::cout << "\t";print_value(get_object(my_object)->get("z"));std::cout << std::endl;;
get_object(LuaValue(my_object))->set("x", LuaValue(99.0));
print_value("my_object.x (after attempted set):");std::cout << "\t";print_value(get_object(my_object)->get("x"));std::cout << std::endl;;
print_value("Begin loop");std::cout << std::endl;;
LuaValue thing = LuaValue(1.0);
for (LuaValue i = LuaValue(1.0); get_double(i) <= get_double(LuaValue(100000.0)); i = LuaValue(get_double(i) + get_double(LuaValue(1.0)))) {
    thing = (get_double(thing) + get_double(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G->get("math"))->get("sin"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", i);
return temp_args; } )())));
}
print_value(thing);std::cout << std::endl;;
std::string test_string = "hello";
print_value("Length of 'hello':");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G->get("string"))->get("len"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", test_string);
return temp_args; } )()));std::cout << std::endl;;
print_value("Reverse of 'hello':");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G->get("string"))->get("reverse"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", test_string);
return temp_args; } )()));std::cout << std::endl;;
std::shared_ptr<LuaObject> my_table = ( [&]() { auto temp_table_1976 = std::make_shared<LuaObject>();
temp_table_1976->set("1", "a");
temp_table_1976->set("2", "b");
temp_table_1976->set("3", "c");
 return temp_table_1976; } )();
print_value("Original table:");std::cout << std::endl;;
for (LuaValue i = LuaValue(1.0); get_double(i) <= get_double(LuaValue(3.0)); i = LuaValue(get_double(i) + get_double(LuaValue(1.0)))) {
    print_value(i);std::cout << "\t";print_value(rawget(get_object(my_table), i));std::cout << std::endl;;
}
print_value("os.clock:");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G->get("os"))->get("clock"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
return temp_args; } )()));std::cout << std::endl;;
print_value("os.time:");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G->get("os"))->get("time"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
return temp_args; } )()));std::cout << std::endl;;
std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G->get("table"))->get("insert"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", my_table);
temp_args->set("2", "d");
return temp_args; } )());
print_value("After insert:");std::cout << std::endl;;
for (LuaValue i = LuaValue(1.0); get_double(i) <= get_double(LuaValue(4.0)); i = LuaValue(get_double(i) + get_double(LuaValue(1.0)))) {
    print_value(i);std::cout << "\t";print_value(rawget(get_object(my_table), i));std::cout << std::endl;;
}
std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G->get("table"))->get("remove"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", my_table);
temp_args->set("2", LuaValue(2.0));
return temp_args; } )());
print_value("After remove:");std::cout << std::endl;;
for (LuaValue i = LuaValue(1.0); get_double(i) <= get_double(LuaValue(3.0)); i = LuaValue(get_double(i) + get_double(LuaValue(1.0)))) {
    print_value(i);std::cout << "\t";print_value(rawget(get_object(my_table), i));std::cout << std::endl;;
}
print_value("package.path:");std::cout << "\t";print_value(get_object(_G->get("package"))->get("path"));std::cout << std::endl;;
print_value("package.cpath:");std::cout << "\t";print_value(get_object(_G->get("package"))->get("cpath"));std::cout << std::endl;;
print_value("_VERSION:");std::cout << "\t";print_value(get_object(_G)->get("_VERSION"));std::cout << std::endl;;
print_value("tonumber('123'):");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G)->get("tonumber"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", "123");
return temp_args; } )()));std::cout << std::endl;;
print_value("tonumber('hello'):");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G)->get("tonumber"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", "hello");
return temp_args; } )()));std::cout << std::endl;;
print_value("tonumber(123):");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G)->get("tonumber"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", LuaValue(123.0));
return temp_args; } )()));std::cout << std::endl;;
print_value("tostring(123):");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G)->get("tostring"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", LuaValue(123.0));
return temp_args; } )()));std::cout << std::endl;;
print_value("tostring('hello'):");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G)->get("tostring"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", "hello");
return temp_args; } )()));std::cout << std::endl;;
print_value("tostring(true):");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G)->get("tostring"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", true);
return temp_args; } )()));std::cout << std::endl;;
LuaValue n = std::monostate{};
print_value("type(n):");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G)->get("type"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", n);
return temp_args; } )()));std::cout << std::endl;;
print_value("type(true):");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G)->get("type"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", true);
return temp_args; } )()));std::cout << std::endl;;
print_value("type(123):");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G)->get("type"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", LuaValue(123.0));
return temp_args; } )()));std::cout << std::endl;;
print_value("type('hello'):");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G)->get("type"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", "hello");
return temp_args; } )()));std::cout << std::endl;;
print_value("type({}):");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G)->get("type"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", ( [&]() { auto temp_table_3353 = std::make_shared<LuaObject>();
 return temp_table_3353; } )());
return temp_args; } )()));std::cout << std::endl;;
print_value("type(function() end):");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G)->get("type"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {
return std::monostate{};
}));
return temp_args; } )()));std::cout << std::endl;;
std::shared_ptr<LuaObject> my_table_with_mt = ( [&]() { auto temp_table_7683 = std::make_shared<LuaObject>();
 return temp_table_7683; } )();
std::shared_ptr<LuaObject> mt_for_get = ( [&]() { auto temp_table_2778 = std::make_shared<LuaObject>();
temp_table_2778->set("__index", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {
    return "metatable_value";
}));
 return temp_table_2778; } )();
get_object(my_table_with_mt)->set_metatable(get_object(mt_for_get));;
print_value("getmetatable(my_table_with_mt):");std::cout << "\t";print_value(std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G)->get("getmetatable"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", my_table_with_mt);
return temp_args; } )()));std::cout << std::endl;;
std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G)->get("pcall"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {
    std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(_G)->get("error"))->func(( [&]() { auto temp_args = std::make_shared<LuaObject>();
temp_args->set("1", "This is an error message");
return temp_args; } )());
return std::monostate{};
}));
return temp_args; } )());

    return 0;
}