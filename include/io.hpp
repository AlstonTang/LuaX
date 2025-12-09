#ifndef IO_HPP
#define IO_HPP

#include "lua_value.hpp" // Include the new LuaValue header
#include "lua_object.hpp" // Include LuaObject after LuaValue

#include <string>
#include <vector>
#include <memory>

class LuaFile : public LuaObject {
public:
	FILE* file_handle;
	bool is_closed;
	bool is_popen; // Track if opened via popen

	LuaFile(const std::string& filename, const std::string& mode);
	LuaFile(FILE* f, bool is_popen_mode = false); // Constructor for existing FILE*
	~LuaFile();

	std::vector<LuaValue> close(std::shared_ptr<LuaObject> args);
	std::vector<LuaValue> flush(std::shared_ptr<LuaObject> args);
	std::vector<LuaValue> lines(std::shared_ptr<LuaObject> args);
	std::vector<LuaValue> read(std::shared_ptr<LuaObject> args);
	std::vector<LuaValue> seek(std::shared_ptr<LuaObject> args);
	std::vector<LuaValue> setvbuf(std::shared_ptr<LuaObject> args);
	std::vector<LuaValue> write(std::shared_ptr<LuaObject> args);
};

// Function to create the 'io' library table
std::shared_ptr<LuaObject> create_io_library();

// Global IO functions
std::vector<LuaValue> io_close(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> io_flush(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> io_input(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> io_lines(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> io_open(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> io_output(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> io_popen(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> io_read(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> io_tmpfile(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> io_type(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> io_write(std::shared_ptr<LuaObject> args);

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