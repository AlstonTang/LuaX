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
int main() {
LuaValue a = LuaValue(10.0);
LuaValue b = LuaValue(20.0);
LuaValue c = (get_double(a) + get_double(b));
print_value("Sum:");std::cout << " ";print_value(c);std::cout << std::endl;;
std::string name = "World";
print_value("Hello,");std::cout << " ";print_value(name);std::cout << std::endl;;
if ((get_double(a) > get_double(b))) {
    print_value("a is greater than b");std::cout << std::endl;;
} else if ((get_double(a) < get_double(b))) {
    print_value("a is less than b");std::cout << std::endl;;
} else {
    print_value("a is equal to b");std::cout << std::endl;;
}

for (LuaValue i = LuaValue(1.0); get_double(i) <= get_double(LuaValue(3.0)); i = LuaValue(get_double(i) + get_double(LuaValue(1.0)))) {
    print_value("For loop iteration:");std::cout << " ";print_value(i);std::cout << std::endl;;
}
LuaValue count = LuaValue(0.0);
std::string text = "hello world";
std::string pattern = "world";
if (std::regex_search(text, std::regex(pattern))) {
    print_value("Pattern found!");std::cout << std::endl;;
}

LuaValue pos = LuaValue(text.find("world") != std::string::npos ? static_cast<double>(text.find("world") + 1) : 0.0);
print_value("Pattern 'world' found at position:");std::cout << " ";print_value(pos);std::cout << std::endl;;
LuaValue new_text = std::regex_replace(text, std::regex("world"), "lua");
print_value("String gsub:");std::cout << " ";print_value(new_text);std::cout << std::endl;;
LuaValue other = other_module::load();
print_value("Module name:");std::cout << " ";print_value(get_object(other)->get("name"));std::cout << std::endl;;
print_value("Module version:");std::cout << " ";print_value(get_object(other)->get("version"));std::cout << std::endl;;
LuaValue greeting = std::get<std::shared_ptr<LuaFunctionWrapper>>(get_object(other)->get("greet"))->func(LuaValue("Gemini"), std::monostate{}, std::monostate{});
print_value(greeting);std::cout << std::endl;;
std::shared_ptr<LuaObject> defaults = ( [&]() { auto temp_table_7831 = std::make_shared<LuaObject>();
temp_table_7831->set("x", LuaValue(0.0));
temp_table_7831->set("y", LuaValue(0.0));
temp_table_7831->set("color", "blue");
 return temp_table_7831; } )();
std::shared_ptr<LuaObject> mt = ( [&]() { auto temp_table_7985 = std::make_shared<LuaObject>();
 return temp_table_7985; } )();
get_object(LuaValue(mt))->set("__index", std::make_shared<LuaFunctionWrapper>(LuaFunctionWrapper{[=](LuaValue table, LuaValue key, LuaValue arg3) -> LuaValue {
    print_value("Accessing missing key:");std::cout << " ";print_value(key);std::cout << std::endl;;
    return defaults;
    key;
}}));
get_object(LuaValue(mt))->set("__newindex", std::make_shared<LuaFunctionWrapper>(LuaFunctionWrapper{[=](LuaValue table, LuaValue key, LuaValue value) -> LuaValue {
    print_value("Attempting to set new key:");std::cout << " ";print_value(key);std::cout << " ";print_value("with value:");std::cout << " ";print_value(value);std::cout << std::endl;;
    if ((get_double(key) == get_double("z"))) {
    get_object(table)->properties[std::get<std::string>(key)] = value;;
} else {
    print_value(to_cpp_string(to_cpp_string("Cannot set key '") + to_cpp_string(key)) + to_cpp_string("'. Use rawset if intended."));std::cout << std::endl;;
}

return std::monostate{};
}}));
std::shared_ptr<LuaObject> my_object = ( [&]() { auto temp_table_9117 = std::make_shared<LuaObject>();
 return temp_table_9117; } )();
get_object(my_object)->set_metatable(get_object(mt));;
print_value("my_object.x:");std::cout << " ";print_value(get_object(my_object)->get("x"));std::cout << std::endl;;
print_value("my_object.color:");std::cout << " ";print_value(get_object(my_object)->get("color"));std::cout << std::endl;;
get_object(LuaValue(my_object))->set("a", LuaValue(100.0));
get_object(LuaValue(my_object))->set("z", LuaValue(50.0));
print_value("my_object.z:");std::cout << " ";print_value(get_object(my_object)->get("z"));std::cout << std::endl;;
get_object(LuaValue(my_object))->set("x", LuaValue(99.0));
print_value("my_object.x (after attempted set):");std::cout << " ";print_value(get_object(my_object)->get("x"));std::cout << std::endl;;

    return 0;
}