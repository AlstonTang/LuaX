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
namespace other_module {
std::shared_ptr<LuaObject> load() {
std::shared_ptr<LuaObject> M = std::make_shared<LuaObject>();
get_object(LuaValue(M))->set("name", "other_module");
get_object(LuaValue(M))->set("version", "1.0");
get_object(LuaValue(M))->set("greet", std::make_shared<LuaFunctionWrapper>(LuaFunctionWrapper{[=](LuaValue name, LuaValue arg2, LuaValue arg3) -> LuaValue {
    print_value("Hello, ");std::cout << " ";print_value(name);std::cout << " ";print_value("!");std::cout << std::endl;;
return std::monostate{};
}}));
return M;
}

} // namespace other_module
