#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <variant>
#include <regex>
#include <functional>
#include "lua_object.hpp"

#include "math.hpp"
#include "string.hpp"
#include "table.hpp"
#include "os.hpp"
#include "io.hpp"
#include "package.hpp"
#include "other_module.hpp"
namespace other_module {
std::shared_ptr<LuaObject> load() {
std::shared_ptr<LuaObject> M = std::make_shared<LuaObject>();
get_object(LuaValue(M))->set("name", "other_module");
get_object(LuaValue(M))->set("version", "1.0");
get_object(LuaValue(M))->set("greet", std::make_shared<LuaFunctionWrapper>([=](std::shared_ptr<LuaObject> args) -> LuaValue {
    LuaValue name = args->get("1");
    return to_cpp_string(to_cpp_string(to_cpp_string(to_cpp_string(to_cpp_string(to_cpp_string("Hello, ") + to_cpp_string(name)) + to_cpp_string(" from ")) + to_cpp_string(get_object(M)->get("name"))) + to_cpp_string(" v")) + to_cpp_string(get_object(M)->get("version"))) + to_cpp_string("!");
}));
return M;
}

} // namespace other_module
