lua src/translate_project.lua > /dev/null
g++ -std=c++17 -Iinclude -o build/luax_app build/main.cpp build/other_module.cpp lib/lua_object.cpp -lstdc++fs
time ./build/luax_app