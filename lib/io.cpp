#include "io.hpp"
#include <iostream>
#include <cstdio>
#include <unistd.h>
#include <vector>
#include <cstring> // For strerror

// Global file handles for stdin, stdout, stderr
std::shared_ptr<LuaFile> io_stdin_handle;
std::shared_ptr<LuaFile> io_stdout_handle;
std::shared_ptr<LuaFile> io_stderr_handle;

// Global state for default input/output
std::shared_ptr<LuaObject> current_input_file;
std::shared_ptr<LuaObject> current_output_file;

// Create a shared metatable for LuaFile objects
static std::shared_ptr<LuaObject> file_metatable = std::make_shared<LuaObject>();

// Helper to create method wrappers for LuaFile
auto make_file_method = [](auto method_ptr) {
	return std::make_shared<LuaFunctionWrapper>([method_ptr](auto args) {
		// The first argument (args.at(0)) will be the LuaFile object itself
		if (auto self_obj = get_object(args.at(0))) {
			if (auto self = std::dynamic_pointer_cast<LuaFile>(self_obj)) {
				return (self.get()->*method_ptr)(args);
			}
		}
		return std::vector<LuaValue>{std::monostate{}, "attempt to call method on a non-file object"};
	});
};

// LuaFile constructor: Opens the file and registers all methods on itself.
LuaFile::LuaFile(const std::string& filename, const std::string& mode) : is_popen(false) {
	file_handle = std::fopen(filename.c_str(), mode.c_str());
	is_closed = (file_handle == nullptr);
}

LuaFile::LuaFile(FILE* f, bool is_popen_mode) : file_handle(f), is_closed(false), is_popen(is_popen_mode) {}

LuaFile::~LuaFile() {
	if (!is_closed && file_handle) {
		if (is_popen) {
			pclose(file_handle);
		} else if (file_handle != stdin && file_handle != stdout && file_handle != stderr) {
			std::fclose(file_handle);
		}
	}
}

std::vector<LuaValue> LuaFile::close(std::vector<LuaValue> args) {
	if (is_closed) return {true}; // Already closed

	int res = 0;
	if (is_popen) {
		res = pclose(file_handle);
	} else {
		res = std::fclose(file_handle);
	}
	is_closed = true;
	file_handle = nullptr;

	if (res == 0) return {true};
	return {std::monostate{}, "close failed"};
}

std::vector<LuaValue> LuaFile::flush(std::vector<LuaValue> args) {
	if (is_closed) return {std::monostate{}, "attempt to use a closed file"};
	if (std::fflush(file_handle) == 0) {
		return {true};
	}
	return {std::monostate{}, "flush failed"};
}

std::vector<LuaValue> LuaFile::read(std::vector<LuaValue> args) {
	if (is_closed) return {std::monostate{}, "attempt to use a closed file"};

	// Lua syntax: file:read(...) or file:read() (defaults to "*l")
	// In this wrapper, the arguments start at key "2"
	std::string format = args.size() >= 2 ? to_cpp_string(args.at(1)) : "*l";

	if (format == "*n") { // Read number
		double num;
		// Skip whitespace is automatic in fscanf with %lf
		if (fscanf(file_handle, "%lf", &num) == 1) {
			return {num};
		}
		return {std::monostate{}};
	} 
	// MODIFIED: Check for both "*a" and "*all"
	else if (format == "*a" || format == "*all") { // Read all (EOF)
		std::string content;
		char buffer[4096]; // Read in 4KB chunks
		while (true) {
			size_t read_count = std::fread(buffer, 1, sizeof(buffer), file_handle);
			if (read_count > 0) {
				content.append(buffer, read_count);
			}
			// If we read less than the buffer size, we hit EOF or Error
			if (read_count < sizeof(buffer)) {
				break;
			}
		}
		
		if (content.empty() && std::feof(file_handle)) {
			return {std::monostate{}};
		}
		return {content};
	} 
	else if (format == "*l" || format == "*L") { // Read line
		std::string line;
		char buffer[1024]; // Chunk size for line reading

		while (true) {
			if (std::fgets(buffer, sizeof(buffer), file_handle) == nullptr) {
				// EOF or Error
				if (line.empty()) {
					return {std::monostate{}}; // EOF before reading anything
				}
				break; // Return what we have so far (partial line at EOF)
			}

			line += buffer;
			
			// Check if we actually found a newline
			if (!line.empty() && line.back() == '\n') {
				// If format is "*l", we strip the newline. For "*L", we keep it.
				if (format == "*l") {
					line.pop_back();
				}
				break;
			}
			// If line didn't end in \n, the buffer filled up but line continues.
			// Loop again to append next chunk.
		}
		return {line};
	} 
	else { // Read specific number of bytes (e.g., file:read(5))
		long long num_bytes = 0;
		try {
			num_bytes = std::stoll(format);
		} catch (...) {
			return {std::monostate{}, "invalid read format"};
		}

		if (num_bytes == 0) return {""}; // Special Lua case: read(0) returns empty string, checks for EOF

		std::vector<char> buffer(num_bytes);
		size_t read_count = std::fread(buffer.data(), 1, num_bytes, file_handle);
		
		if (read_count == 0) {
			 return {std::monostate{}}; // EOF
		}
		
		return {std::string(buffer.data(), read_count)};
	}
}

std::vector<LuaValue> LuaFile::seek(std::vector<LuaValue> args) {
	if (is_closed) return {std::monostate{}, "attempt to use a closed file"};

	std::string whence = args.size() >= 2 ? to_cpp_string(args.at(1)) : "cur";
	long long offset = args.size() >= 3 ? get_long_long(args.at(2)) : 0;

	int origin = SEEK_CUR;
	if (whence == "set") origin = SEEK_SET;
	else if (whence == "end") origin = SEEK_END;

	if (std::fseek(file_handle, offset, origin) == 0) {
		long long pos = std::ftell(file_handle);
		return {static_cast<double>(pos)};
	}
	return {std::monostate{}, "seek failed"};
}

std::vector<LuaValue> LuaFile::setvbuf(std::vector<LuaValue> args) {
	if (is_closed) return {std::monostate{}, "attempt to use a closed file"};

	std::string mode_str = to_cpp_string(args.at(1));
	long long size = args.size() >= 3 ? get_long_long(args.at(2)) : 1024; // Default size

	int mode = _IOFBF;
	if (mode_str == "no") mode = _IONBF;
	else if (mode_str == "line") mode = _IOLBF;

	if (std::setvbuf(file_handle, nullptr, mode, size) == 0) {
		return {true};
	}
	return {std::monostate{}, "setvbuf failed"};
}

std::vector<LuaValue> LuaFile::write(std::vector<LuaValue> args) {
	if (is_closed) return {std::monostate{}, "attempt to use a closed file"};

	// Lua: file:write(a, b, c) -> C++ args are { "1": file, "2": a, "3": b, "4": c }
	// So we iterate from index 2.
	for (int i = 1; i < args.size(); ++i) {
		std::string s = to_cpp_string(args.at(i));
		if (std::fputs(s.c_str(), file_handle) == EOF) {
			 return {std::monostate{}, "write failed"};
		}
	}
	return {shared_from_this()}; // Return the file handle
}

std::vector<LuaValue> LuaFile::lines(std::vector<LuaValue> args) {
	if (is_closed) return {std::monostate{}, "attempt to use a closed file"};

	// The iterator function for file:lines()
	auto iterator_func = std::make_shared<LuaFunctionWrapper>([self_obj = shared_from_this()](auto) -> std::vector<LuaValue> {
		// Cast self_obj to LuaFile to access file_handle
		if (auto self = std::dynamic_pointer_cast<LuaFile>(self_obj)) {
			if (self->is_closed) return {std::monostate{}};
			char buffer[4096];
			if (std::fgets(buffer, sizeof(buffer), self->file_handle)) {
				std::string line(buffer);
				if (!line.empty() && line.back() == '\n') line.pop_back();
				return {line};
			}
		}
		return {std::monostate{}}; // End of iteration by returning nil
	});

	// Lua's for-loop iterator protocol returns: iterator_function, state, initial_value
	return { iterator_func, shared_from_this(), std::monostate{} };
}

// --- Global `io` Library Functions ---

std::vector<LuaValue> io_open(std::vector<LuaValue> args) {
	std::string filename = to_cpp_string(args.at(0));
	std::string mode = args.size() >= 2 ? to_cpp_string(args.at(1)) : "r";

	auto file_obj = std::make_shared<LuaFile>(filename, mode);

	if (file_obj->is_closed) {
		return {std::monostate{}, "cannot open file '" + filename + "': " + std::strerror(errno)};
	}
	// Attach the shared metatable to the new LuaFile instance
	file_obj->set_metatable(file_metatable);
	return {file_obj};
}

std::vector<LuaValue> io_popen(std::vector<LuaValue> args) {
	std::string command = to_cpp_string(args.at(0));
	std::string mode = args.size() >= 2 ? to_cpp_string(args.at(1)) : "r";

	FILE* f = popen(command.c_str(), mode.c_str());
	if (!f) {
		return {std::monostate{}, "popen failed: " + std::string(std::strerror(errno))};
	}

	auto file_obj = std::make_shared<LuaFile>(f, true);
	file_obj->set_metatable(file_metatable);
	return {file_obj};
}

std::vector<LuaValue> io_tmpfile(std::vector<LuaValue> args) {
	FILE* f = std::tmpfile();
	if (!f) {
		return {std::monostate{}, "tmpfile failed: " + std::string(std::strerror(errno))};
	}
	auto file_obj = std::make_shared<LuaFile>(f, false);
	file_obj->set_metatable(file_metatable);
	return {file_obj};
}

std::vector<LuaValue> io_type(std::vector<LuaValue> args) {
	LuaValue val = args.at(0);
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) {
		auto obj = std::get<std::shared_ptr<LuaObject>>(val);
		if (auto file_handle = std::dynamic_pointer_cast<LuaFile>(obj)) {
			return {file_handle->is_closed ? "closed file" : "file"};
		}
	}
	return {std::monostate{}}; // nil for non-file objects
}

std::vector<LuaValue> io_input(std::vector<LuaValue> args) {
	LuaValue arg = args.at(0);
	if (std::holds_alternative<std::monostate>(arg)) {
		return {current_input_file};
	} else if (std::holds_alternative<std::string>(arg)) {
		auto new_file_vec = io_open({arg, "r"});
		if (std::holds_alternative<std::shared_ptr<LuaObject>>(new_file_vec[0])) {
			current_input_file = std::get<std::shared_ptr<LuaObject>>(new_file_vec[0]);
			return {current_input_file};
		}
		return new_file_vec; // Return nil and error message on failure
	} else if (std::holds_alternative<std::shared_ptr<LuaObject>>(arg)) {
		current_input_file = std::get<std::shared_ptr<LuaObject>>(arg);
		return {current_input_file};
	}
	return {std::monostate{}};
}

std::vector<LuaValue> io_output(std::vector<LuaValue> args) {
	LuaValue arg = args.at(0);
	if (std::holds_alternative<std::monostate>(arg)) {
		return {current_output_file};
	} else if (std::holds_alternative<std::string>(arg)) {
		auto new_file_vec = io_open({arg, "w"});
		if (std::holds_alternative<std::shared_ptr<LuaObject>>(new_file_vec[0])) {
			current_output_file = std::get<std::shared_ptr<LuaObject>>(new_file_vec[0]);
			return {current_output_file};
		}
		return new_file_vec; // Return nil and error message on failure
	} else if (std::holds_alternative<std::shared_ptr<LuaObject>>(arg)) {
		current_output_file = std::get<std::shared_ptr<LuaObject>>(arg);
		return {current_output_file};
	}
	return {std::monostate{}};
}

std::vector<LuaValue> io_close(std::vector<LuaValue> args) {
	LuaValue file_val = args.size() >= 1 ? args.at(0) : LuaValue(current_output_file);
	if (auto file_obj = get_object(file_val)) {
		auto close_func_val = file_obj->get("close");
		if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(close_func_val)) {
			auto close_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(close_func_val);
			return close_func->func({file_obj});
		}
	}
	return {std::monostate{}, "invalid file handle"};
}

std::vector<LuaValue> io_read(std::vector<LuaValue> args) {
	auto read_func_val = current_input_file->get("read");
	if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(read_func_val)) {
		auto read_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(read_func_val);
		std::vector<LuaValue> method_args = {current_input_file};
		method_args.insert(method_args.end(), args.begin(), args.end());
		return read_func->func(method_args);
	}
	return {std::monostate{}, "input file is not readable"};
}

std::vector<LuaValue> io_write(std::vector<LuaValue> args) {
	auto write_func_val = current_output_file->get("write");
	if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(write_func_val)) {
		auto write_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(write_func_val);
		std::vector<LuaValue> method_args = {current_input_file};
		method_args.insert(method_args.end(), args.begin(), args.end());
		return write_func->func(method_args);
	}
	return {std::monostate{}, "output file is not writable"};
}

std::vector<LuaValue> io_flush(std::vector<LuaValue> args) {
	auto flush_func_val = current_output_file->get("flush");
	if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(flush_func_val)) {
		auto flush_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(flush_func_val);
		return flush_func->func({current_output_file});
	}
	return {std::monostate{}, "output file is not flushable"};
}

std::vector<LuaValue> io_lines(std::vector<LuaValue> args) {
	LuaValue filename_val = args.at(0);
	if (std::holds_alternative<std::monostate>(filename_val)) {
		// io.lines() -> read from default input
		auto lines_func_val = current_input_file->get("lines");
		if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(lines_func_val)) {
			auto lines_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(lines_func_val);
			return lines_func->func({current_input_file});
		}
	} else {
		// io.lines(filename) -> open file and iterate
		auto open_res = io_open({filename_val, "r"});
		if (std::holds_alternative<std::shared_ptr<LuaObject>>(open_res[0])) {
			auto file_obj = std::get<std::shared_ptr<LuaObject>>(open_res[0]);
			auto lines_func_val = file_obj->get("lines");
			 if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(lines_func_val)) {
				auto lines_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(lines_func_val);
				
				auto method_args = std::make_shared<LuaObject>();
				auto original_iter = lines_func->func({file_obj})[0];
				if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(original_iter)) {
					 auto iter_wrapper = std::make_shared<LuaFunctionWrapper>([original_iter, file_obj](auto args) -> std::vector<LuaValue> {
						auto func = std::get<std::shared_ptr<LuaFunctionWrapper>>(original_iter);
						auto res = func->func(args);
						if (res.empty() || std::holds_alternative<std::monostate>(res[0])) {
							// Close file
							if (auto f = std::dynamic_pointer_cast<LuaFile>(file_obj)) {
								f->close({});
							}
						}
						return res;
					});
					return {iter_wrapper, std::monostate{}, std::monostate{}};
				}
			}
		} else {
			 throw std::runtime_error("cannot open file '" + to_cpp_string(filename_val) + "'");
		}
	}
	return {std::monostate{}};
}

// --- Library Creation ---

std::shared_ptr<LuaObject> create_io_library() {
	static std::shared_ptr<LuaObject> io_lib;
	if (io_lib) return io_lib;

	io_lib = std::make_shared<LuaObject>();

	file_metatable->properties = {
		{"close", make_file_method(&LuaFile::close)},
		{"flush", make_file_method(&LuaFile::flush)},
		{"read",  make_file_method(&LuaFile::read)},
		{"seek",  make_file_method(&LuaFile::seek)},
		{"setvbuf", make_file_method(&LuaFile::setvbuf)},
		{"write", make_file_method(&LuaFile::write)},
		{"lines", make_file_method(&LuaFile::lines)},
		{"__index", file_metatable}
	};

	// Set up standard file handles
	io_stdin_handle = std::make_shared<LuaFile>(stdin, false);
	io_stdin_handle->set_metatable(file_metatable);
	
	io_stdout_handle = std::make_shared<LuaFile>(stdout, false);
	io_stdout_handle->set_metatable(file_metatable);
	
	io_stderr_handle = std::make_shared<LuaFile>(stderr, false);
	io_stderr_handle->set_metatable(file_metatable);

	current_input_file = io_stdin_handle;
	current_output_file = io_stdout_handle;

	io_lib->properties = {
		{"close", std::make_shared<LuaFunctionWrapper>(io_close)},
		{"flush", std::make_shared<LuaFunctionWrapper>(io_flush)},
		{"input", std::make_shared<LuaFunctionWrapper>(io_input)},
		{"lines", std::make_shared<LuaFunctionWrapper>(io_lines)},
		{"open", std::make_shared<LuaFunctionWrapper>(io_open)},
		{"output", std::make_shared<LuaFunctionWrapper>(io_output)},
		{"popen", std::make_shared<LuaFunctionWrapper>(io_popen)},
		{"read", std::make_shared<LuaFunctionWrapper>(io_read)},
		{"tmpfile", std::make_shared<LuaFunctionWrapper>(io_tmpfile)},
		{"type", std::make_shared<LuaFunctionWrapper>(io_type)},
		{"write", std::make_shared<LuaFunctionWrapper>(io_write)},
		{"stdin", io_stdin_handle},
		{"stdout", io_stdout_handle},
		{"stderr", io_stderr_handle}
	};

	return io_lib;
}