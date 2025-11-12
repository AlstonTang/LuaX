#include "io.hpp"
#include "lua_object.hpp"
#include <iostream>
#include <fstream>
#include <string>

// io.write
LuaValue io_write(std::shared_ptr<LuaObject> args) {
    for (int i = 1; ; ++i) {
        LuaValue val = args->get(std::to_string(i));
        if (std::holds_alternative<std::monostate>(val)) break;
        std::cout << to_cpp_string(val);
    }
    return std::monostate{};
}

// io.read
LuaValue io_read(std::shared_ptr<LuaObject> args) {
    std::string format = "*l"; // Default to read line
    if (args->properties.count("1")) {
        format = std::get<std::string>(args->get("1"));
    }

    if (format == "*l") {
        std::string line;
        std::getline(std::cin, line);
        return line;
    } else if (format == "*a") {
        std::string content((std::istreambuf_iterator<char>(std::cin)),
                            std::istreambuf_iterator<char>());
        return content;
    } else if (format == "*n") {
        double num;
        std::cin >> num;
        return num;
    }
    return std::monostate{};
}

std::shared_ptr<LuaObject> create_io_library() {
    auto io_lib = std::make_shared<LuaObject>();

    io_lib->set("write", std::make_shared<LuaFunctionWrapper>(io_write));
    io_lib->set("read", std::make_shared<LuaFunctionWrapper>(io_read));

    return io_lib;
}
