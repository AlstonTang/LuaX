#ifndef IO_HPP
#define IO_HPP

#include "lua_object.hpp"
#include <memory>
#include <fstream>

// Forward declaration for LuaFile
class LuaFile;

// LuaFile class to wrap std::fstream
class LuaFile : public LuaObject { // Inherit from LuaObject
public:
    std::fstream file_stream;
    bool is_closed = false;

    LuaFile(const std::string& filename, const std::string& mode);
    ~LuaFile();

    // File object methods
    std::vector<LuaValue> close(std::shared_ptr<LuaObject> args);
    std::vector<LuaValue> flush(std::shared_ptr<LuaObject> args);
    std::vector<LuaValue> lines(std::shared_ptr<LuaObject> args); // Returns an iterator
    std::vector<LuaValue> read(std::shared_ptr<LuaObject> args);
    std::vector<LuaValue> seek(std::shared_ptr<LuaObject> args);
    std::vector<LuaValue> write(std::shared_ptr<LuaObject> args);
};

std::shared_ptr<LuaObject> create_io_library();

#endif // IO_HPP
