#ifndef IO_HPP
#define IO_HPP

#include "lua_value.hpp" // Include the new LuaValue header
#include "lua_object.hpp" // Include LuaObject after LuaValue

#include <fstream>
#include <string>
#include <vector>
#include <memory>

// LuaFile is now a specialized LuaObject
class LuaFile : public LuaObject {
public:
    std::fstream file_stream;
    bool is_closed;

    LuaFile(const std::string& filename, const std::string& mode);
    ~LuaFile();

    // These are the internal implementations of the file methods
    std::vector<LuaValue> close(std::shared_ptr<LuaObject> args);
    std::vector<LuaValue> flush(std::shared_ptr<LuaObject> args);
    std::vector<LuaValue> lines(std::shared_ptr<LuaObject> args);
    std::vector<LuaValue> read(std::shared_ptr<LuaObject> args);
    std::vector<LuaValue> seek(std::shared_ptr<LuaObject> args);
    std::vector<LuaValue> write(std::shared_ptr<LuaObject> args);
};

// Function to create the 'io' library table
std::shared_ptr<LuaObject> create_io_library();

// Helper to safely get a LuaFile from a LuaValue. Throws on type error.
inline std::shared_ptr<LuaFile> get_file(const LuaValue& value) {
    auto obj = get_object(value); // First, get the base LuaObject
    // Then, safely downcast to a LuaFile. Returns nullptr if the cast fails.
    auto file_ptr = std::dynamic_pointer_cast<LuaFile>(obj);
    if (!file_ptr) {
        throw std::runtime_error("Type error: expected a file handle.");
    }
    return file_ptr;
}

#endif