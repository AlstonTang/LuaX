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
	~LuaFile() override;

	void close(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
	void flush(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) const;
	void lines(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
	void read(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) const;
	void seek(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) const;
	void setvbuf(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) const;
	void write(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
};

// Function to create the 'io' library table
std::shared_ptr<LuaObject> create_io_library();

// Global IO functions
void io_close(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void io_flush(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void io_input(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void io_lines(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void io_open(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void io_output(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void io_popen(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void io_read(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void io_tmpfile(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void io_type(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);
void io_write(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out);

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
