#include "io.hpp"
#include <iostream>
#include <cstdio>
#include <vector>
#include <cstring> // For strerror
#include <cerrno>

// Platform specific for popen
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#endif

// Global file handles for stdin, stdout, stderr
std::shared_ptr<LuaFile> io_stdin_handle;
std::shared_ptr<LuaFile> io_stdout_handle;
std::shared_ptr<LuaFile> io_stderr_handle;

// Global state for default input/output
std::shared_ptr<LuaObject> current_input_file;
std::shared_ptr<LuaObject> current_output_file;

inline LuaValue file_to_value(std::shared_ptr<LuaFile> f) {
	return LuaValue(std::static_pointer_cast<LuaObject>(f));
}

// Create a shared metatable for LuaFile objects
static std::shared_ptr<LuaObject> file_metatable = std::make_shared<LuaObject>();

// Helper to create method wrappers for LuaFile
// The lambda must return void and take the output vector reference.
auto make_file_method = [](auto method_ptr) {
	return std::make_shared<LuaFunctionWrapper>(
		[method_ptr](const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
			// The first argument (args[0]) will be the LuaFile object itself
			if (n_args < 1) {
				out.assign({std::monostate{}, "attempt to call method on a non-file object"});
				return;
			}

			if (auto self_obj = get_object(args[0])) {
				if (auto self = std::dynamic_pointer_cast<LuaFile>(self_obj)) {
					// Call the member function directly with the output buffer
					(self.get()->*method_ptr)(args, n_args, out);
					return;
				}
			}

			out.assign({std::monostate{}, "attempt to call method on a non-file object"});
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
		}
		else if (file_handle != stdin && file_handle != stdout && file_handle != stderr) {
			std::fclose(file_handle);
		}
	}
}

void LuaFile::close(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (is_closed) {
		out.assign({true});
		return;
	}

	int res = 0;
	if (is_popen) {
		res = pclose(file_handle);
	}
	else {
		res = std::fclose(file_handle);
	}
	is_closed = true;
	file_handle = nullptr;

	if (res == 0) {
		out.assign({true});
	}
	else {
		out.assign({std::monostate{}, "close failed"});
	}
}

void LuaFile::flush(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (is_closed) {
		out.assign({std::monostate{}, "attempt to use a closed file"});
		return;
	}
	if (std::fflush(file_handle) == 0) {
		out.assign({true});
	}
	else {
		out.assign({std::monostate{}, "flush failed"});
	}
}

void LuaFile::read(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (is_closed) {
		out.assign({std::monostate{}, "attempt to use a closed file"});
		return;
	}

	// Lua syntax: file:read(...) or file:read() (defaults to "*l")
	// args[0] is self. args[1] is first arg.
	std::string format = n_args >= 2 ? to_cpp_string(args[1]) : "*l";

	if (format == "*n") { // Read number
		double num;
		if (fscanf(file_handle, "%lf", &num) == 1) {
			out.assign({num});
		}
		else {
			out.assign({std::monostate{}});
		}
	}
	else if (format == "*a" || format == "*all") { // Read all (EOF)
		std::string content;
		char buffer[4096];
		while (true) {
			size_t read_count = std::fread(buffer, 1, sizeof(buffer), file_handle);
			if (read_count > 0) {
				content.append(buffer, read_count);
			}
			if (read_count < sizeof(buffer)) break;
		}

		if (content.empty() && std::feof(file_handle)) {
			out.assign({std::monostate{}});
		}
		else {
			out.assign({content});
		}
	}
	else if (format == "*l" || format == "*L") { // Read line
		std::string line;
		char buffer[1024];

		while (true) {
			if (std::fgets(buffer, sizeof(buffer), file_handle) == nullptr) {
				if (line.empty()) {
					out.assign({std::monostate{}}); // EOF before reading anything
					return;
				}
				break;
			}

			line += buffer;

			if (!line.empty() && line.back() == '\n') {
				if (format == "*l") {
					line.pop_back();
				}
				break;
			}
		}
		out.assign({line});
	}
	else { // Read bytes
		long long num_bytes = 0;
		try {
			num_bytes = std::stoll(format);
		}
		catch (...) {
			out.assign({std::monostate{}, "invalid read format"});
			return;
		}

		if (num_bytes == 0) {
			// Check for EOF without reading
			int c = std::fgetc(file_handle);
			if (c == EOF) {
				out.assign({std::monostate{}});
			}
			else {
				std::ungetc(c, file_handle);
				out.assign({""});
			}
			return;
		}

		std::vector<char> buffer(num_bytes);
		size_t read_count = std::fread(buffer.data(), 1, num_bytes, file_handle);

		if (read_count == 0) {
			out.assign({std::monostate{}});
		}
		else {
			out.assign({std::string(buffer.data())});
		}
	}
}

void LuaFile::seek(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (is_closed) {
		out.assign({std::monostate{}, "attempt to use a closed file"});
		return;
	}

	std::string whence = n_args >= 2 ? to_cpp_string(args[1]) : "cur";
	long long offset = n_args >= 3 ? get_long_long(args[2]) : 0;

	int origin = SEEK_CUR;
	if (whence == "set") origin = SEEK_SET;
	else if (whence == "end") origin = SEEK_END;

	if (std::fseek(file_handle, offset, origin) == 0) {
		long long pos = std::ftell(file_handle);
		out.assign({pos});
	}
	else {
		out.assign({std::monostate{}, "seek failed"});
	}
}

void LuaFile::setvbuf(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.clear();
	if (is_closed) {
		out.push_back(std::monostate{});
		out.push_back("attempt to use a closed file");
		return;
	}

	std::string mode_str = to_cpp_string(args[1]);
	long long size = n_args >= 3 ? get_long_long(args[2]) : 1024;

	int mode = _IOFBF;
	if (mode_str == "no") mode = _IONBF;
	else if (mode_str == "line") mode = _IOLBF;

	if (std::setvbuf(file_handle, nullptr, mode, size) == 0) {
		out.push_back(true);
	}
	else {
		out.push_back(std::monostate{});
		out.push_back("setvbuf failed");
	}
}

void LuaFile::write(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.clear();
	if (is_closed) {
		out.push_back(std::monostate{});
		out.push_back("attempt to use a closed file");
		return;
	}

	// args[0] is self. Write args[1]...args[N]
	for (size_t i = 1; i < n_args; ++i) {
		std::string s = to_cpp_string(args[i]);
		if (std::fputs(s.c_str(), file_handle) == EOF) {
			out.push_back(std::monostate{});
			out.push_back("write failed");
			return;
		}
	}
	out.push_back(shared_from_this());
}

void LuaFile::lines(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (is_closed) {
		out.assign({std::monostate{}, "attempt to use a closed file"});
		return;
	}

	// The iterator function for file:lines()
	auto iterator_func = std::make_shared<LuaFunctionWrapper>(
		[self_obj = shared_from_this()](const LuaValue* _, size_t __, std::vector<LuaValue>& iter_out) {
			// Cast self_obj to LuaFile to access file_handle
			if (auto self = std::dynamic_pointer_cast<LuaFile>(self_obj)) {
				if (self->is_closed) {
					iter_out.assign({std::monostate{}});
					return;
				}
				char buffer[4096];
				if (std::fgets(buffer, sizeof(buffer), self->file_handle)) {
					std::string line(buffer);
					if (!line.empty() && line.back() == '\n') line.pop_back();
					iter_out.assign({line});
					return;
				}
			}
			iter_out.assign({std::monostate{}}); // End of iteration
		});

	// Return { iterator, self, nil }
	out.assign({iterator_func, shared_from_this(), std::monostate{}});
}

// --- Global `io` Library Functions ---

void io_open(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string filename = to_cpp_string(args[0]);
	std::string mode = n_args >= 2 ? to_cpp_string(args[1]) : "r";

	auto file_obj = std::make_shared<LuaFile>(filename, mode);

	if (file_obj->is_closed) {
		out.assign({std::monostate{}, "cannot open file '" + filename + "': " + std::string(std::strerror(errno))});
		return;
	}
	// Attach the shared metatable to the new LuaFile instance
	file_obj->set_metatable(file_metatable);
	out.assign({file_to_value(file_obj)});
}

void io_popen(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string command = to_cpp_string(args[0]);
	std::string mode = n_args >= 2 ? to_cpp_string(args[1]) : "r";

	FILE* f = popen(command.c_str(), mode.c_str());
	if (!f) {
		out.assign({std::monostate{}, "popen failed: " + std::string(std::strerror(errno))});
		return;
	}

	auto file_obj = std::make_shared<LuaFile>(f, true);
	file_obj->set_metatable(file_metatable);
	out.assign({file_to_value(file_obj)});
}

void io_tmpfile(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	FILE* f = std::tmpfile();
	if (!f) {
		out.assign({std::monostate{}, "tmpfile failed: " + std::string(std::strerror(errno))});
		return;
	}
	auto file_obj = std::make_shared<LuaFile>(f, false);
	file_obj->set_metatable(file_metatable);
	out.assign({file_to_value(file_obj)});
}

void io_type(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	LuaValue val = args[0];
	if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) {
		auto obj = std::get<std::shared_ptr<LuaObject>>(val);
		if (auto file_handle = std::dynamic_pointer_cast<LuaFile>(obj)) {
			out.assign({file_handle->is_closed ? "closed file" : "file"});
			return;
		}
	}
	out.assign({std::monostate{}});
}

void io_input(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	// io.input([file])
	if (n_args == 0) {
		out.assign({current_input_file});
		return;
	}

	LuaValue arg = args[0];
	if (std::holds_alternative<std::monostate>(arg)) {
		out.assign({current_input_file});
	}
	else if (std::holds_alternative<std::string>(arg)) {
		// Open the file
		LuaValue open_args[] = {arg, LuaValue("r")};
		std::vector<LuaValue> res;
		io_open(open_args, 2, res);

		if (!res.empty() && std::holds_alternative<std::shared_ptr<LuaObject>>(res[0])) {
			current_input_file = std::get<std::shared_ptr<LuaObject>>(res[0]);
			out.assign({current_input_file});
		}
		else {
			out.assign(res.begin(), res.end()); // Error
		}
	}
	else if (std::holds_alternative<std::shared_ptr<LuaObject>>(arg)) {
		current_input_file = std::get<std::shared_ptr<LuaObject>>(arg);
		out.assign({current_input_file});
	}
	else {
		out.assign({std::monostate{}});
	}
}

void io_output(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	// io.output([file])
	if (n_args == 0) {
		out.assign({current_output_file});
		return;
	}

	LuaValue arg = args[0];
	if (std::holds_alternative<std::monostate>(arg)) {
		out.assign({current_output_file});
	}
	else if (std::holds_alternative<std::string>(arg)) {
		LuaValue open_args[] = {arg, LuaValue("w")};
		std::vector<LuaValue> res;
		io_open(open_args, 2, res);

		if (!res.empty() && std::holds_alternative<std::shared_ptr<LuaObject>>(res[0])) {
			current_output_file = std::get<std::shared_ptr<LuaObject>>(res[0]);
			out.assign({current_output_file});
		}
		else {
			out.assign(res.begin(), res.end());
		}
	}
	else if (std::holds_alternative<std::shared_ptr<LuaObject>>(arg)) {
		current_output_file = std::get<std::shared_ptr<LuaObject>>(arg);
		out.assign({current_output_file});
	}
	else {
		out.assign({std::monostate{}});
	}
}

void io_close(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	// io.close([file])
	LuaValue file_val = n_args >= 1 ? args[0] : LuaValue(current_output_file);

	if (auto file_obj = get_object(file_val)) {
		auto close_func_val = file_obj->get("close");
		if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(close_func_val)) {
			auto close_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(close_func_val);
			LuaValue args_to_pass[] = {file_obj};
			// Forward call with our output buffer
			out.clear();
			close_func->func(args_to_pass, 1, out);
			return;
		}
	}
	out.assign({std::monostate{}, "invalid file handle"});
}

void io_read(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	auto read_func_val = current_input_file->get("read");
	if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(read_func_val)) {
		auto read_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(read_func_val);
		std::vector<LuaValue> method_args = {current_input_file};
		for (size_t i = 0; i < n_args; i++) method_args.push_back(args[i]);
		out.clear();
		read_func->func(method_args.data(), method_args.size(), out);
		return;
	}
	out.assign({std::monostate{}, "input file is not readable"});
}

void io_write(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	auto write_func_val = current_output_file->get("write");
	if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(write_func_val)) {
		auto write_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(write_func_val);
		std::vector<LuaValue> method_args = {current_output_file};
		for (size_t i = 0; i < n_args; i++) method_args.push_back(args[i]);
		out.clear();
		write_func->func(method_args.data(), method_args.size(), out);
		return;
	}
	out.assign({std::monostate{}, "output file is not writable"});
}

void io_flush(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	auto flush_func_val = current_output_file->get("flush");
	if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(flush_func_val)) {
		auto flush_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(flush_func_val);
		LuaValue f_args[] = {current_output_file};
		out.clear();
		flush_func->func(f_args, 1, out);
		return;
	}
	out.assign({std::monostate{}, "output file is not flushable"});
}

void io_lines(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	LuaValue filename_val = (n_args > 0) ? args[0] : LuaValue(std::monostate{});

	if (std::holds_alternative<std::monostate>(filename_val)) {
		// io.lines() -> read from default input
		auto lines_func_val = current_input_file->get("lines");
		if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(lines_func_val)) {
			auto lines_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(lines_func_val);
			LuaValue l_args[] = {current_input_file};
			out.clear();
			lines_func->func(l_args, 1, out);
			return;
		}
	}
	else {
		// io.lines(filename) -> open file and iterate
		LuaValue open_args[] = {filename_val, LuaValue("r")};
		std::vector<LuaValue> open_res;
		io_open(open_args, 2, open_res);

		if (!open_res.empty() && std::holds_alternative<std::shared_ptr<LuaObject>>(open_res[0])) {
			auto file_obj = std::get<std::shared_ptr<LuaObject>>(open_res[0]);
			auto lines_func_val = file_obj->get("lines");

			if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(lines_func_val)) {
				auto lines_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(lines_func_val);

				// Get the file iterator: file:lines() returns (iter, self, nil)
				std::vector<LuaValue> iter_res;
				LuaValue l_args[] = {file_obj};
				lines_func->func(l_args, 1, iter_res);

				if (!iter_res.empty() && std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(iter_res[0])) {
					auto original_iter = std::get<std::shared_ptr<LuaFunctionWrapper>>(iter_res[0]);

					// Wrap the iterator to close the file on nil
					auto iter_wrapper = std::make_shared<LuaFunctionWrapper>(
						[original_iter, file_obj](const LuaValue* w_args, size_t w_n_args,
						                          std::vector<LuaValue>& w_out) {
							// Call original iterator
							original_iter->func(w_args, w_n_args, w_out);

							// If returns nil, close the file
							if (w_out.empty() || std::holds_alternative<std::monostate>(w_out[0])) {
								if (auto f = std::dynamic_pointer_cast<LuaFile>(file_obj)) {
									std::vector<LuaValue> ignored_out;
									f->close(nullptr, 0, ignored_out);
								}
							}
						});

					out.assign({iter_wrapper, std::monostate{}, std::monostate{}});
					return;
				}
			}
		}
		else {
			throw std::runtime_error("cannot open file '" + to_cpp_string(filename_val) + "'");
		}
	}
	out.assign({std::monostate{}});
}

// --- Library Creation ---

std::shared_ptr<LuaObject> create_io_library() {
	static std::shared_ptr<LuaObject> io_lib;
	if (io_lib) return io_lib;

	io_lib = std::make_shared<LuaObject>();

	file_metatable->properties = {
		{"close", make_file_method(&LuaFile::close)},
		{"flush", make_file_method(&LuaFile::flush)},
		{"read", make_file_method(&LuaFile::read)},
		{"seek", make_file_method(&LuaFile::seek)},
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

	current_input_file = std::static_pointer_cast<LuaObject>(io_stdin_handle);
	current_output_file = std::static_pointer_cast<LuaObject>(io_stdout_handle);

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
		{"stdin", file_to_value(io_stdin_handle)},
		{"stdout", file_to_value(io_stdout_handle)},
		{"stderr", file_to_value(io_stderr_handle)}
	};

	return io_lib;
}
