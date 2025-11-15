#include "io.hpp"
#include <iostream>
#include <cstdio>
#include <unistd.h>
#include <vector>

// Global file handles for stdin, stdout, stderr
std::shared_ptr<LuaObject> io_stdin_handle = std::make_shared<LuaObject>();
std::shared_ptr<LuaObject> io_stdout_handle = std::make_shared<LuaObject>();
std::shared_ptr<LuaObject> io_stderr_handle = std::make_shared<LuaObject>();

// Global state for default input/output
std::shared_ptr<LuaObject> current_input_file = io_stdin_handle;
std::shared_ptr<LuaObject> current_output_file = io_stdout_handle;

// Create a shared metatable for LuaFile objects
static std::shared_ptr<LuaObject> file_metatable = std::make_shared<LuaObject>();

// Helper to create method wrappers for LuaFile
auto make_file_method = [](auto method_ptr) {
    return std::make_shared<LuaFunctionWrapper>([method_ptr](auto args) {
        // The first argument (args->get("1")) will be the LuaFile object itself
        if (auto self_obj = get_object(args->get("1"))) {
            if (auto self = std::dynamic_pointer_cast<LuaFile>(self_obj)) {
                return (self.get()->*method_ptr)(args);
            }
        }
        return std::vector<LuaValue>{std::monostate{}, "attempt to call method on a non-file object"};
    });
};

// --- LuaFile Implementation ---

// LuaFile constructor: Opens the file and registers all methods on itself.
LuaFile::LuaFile(const std::string& filename, const std::string& mode) {
    std::ios_base::openmode open_mode = std::ios_base::in; // Default to read
    if (mode.find('w') != std::string::npos) open_mode = std::ios_base::out | std::ios_base::trunc;
    else if (mode.find('a') != std::string::npos) open_mode = std::ios_base::out | std::ios_base::app;
    if (mode.find('+') != std::string::npos) open_mode |= std::ios_base::in;
    if (mode.find('b') != std::string::npos) open_mode |= std::ios_base::binary;

    file_stream.open(filename, open_mode);
    is_closed = !file_stream.is_open();
}

LuaFile::~LuaFile() {
    if (file_stream.is_open()) {
        file_stream.close();
    }
}

std::vector<LuaValue> LuaFile::close(std::shared_ptr<LuaObject> args) {
    if (file_stream.is_open()) {
        file_stream.close();
        is_closed = true;
    }
    return {true};
}

std::vector<LuaValue> LuaFile::flush(std::shared_ptr<LuaObject> args) {
    if (file_stream.is_open()) {
        file_stream.flush();
        return {true};
    }
    return {std::monostate{}, "cannot flush closed file"};
}

std::vector<LuaValue> LuaFile::read(std::shared_ptr<LuaObject> args) {
    if (is_closed) return {std::monostate{}, "attempt to use a closed file"};

    // Default to reading a line. The first argument to the method is at key "2".
    std::string format = args->properties.count("2") ? to_cpp_string(args->get("2")) : "*l";

    if (format == "*l") { // read line
        std::string line;
        if (std::getline(file_stream, line)) return {line};
    } else if (format == "*a") { // read all
        std::string content((std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>());
        return {content};
    } else if (format == "*n") { // read number
        double num;
        file_stream >> num;
        if (file_stream.fail()) {
            file_stream.clear();
            return {std::monostate{}};
        }
        return {num};
    } else { // read N bytes
        try {
            long long num_bytes = std::stoll(format);
            std::vector<char> buffer(num_bytes);
            file_stream.read(buffer.data(), num_bytes);
            return {std::string(buffer.data(), file_stream.gcount())};
        } catch (...) { /* Invalid format */ }
    }
    return {std::monostate{}}; // EOF or error
}

std::vector<LuaValue> LuaFile::seek(std::shared_ptr<LuaObject> args) {
    if (is_closed) return {std::monostate{}, "attempt to use a closed file"};

    std::string whence = args->properties.count("2") ? to_cpp_string(args->get("2")) : "cur";
    long long offset = args->properties.count("3") ? get_long_long(args->get("3")) : 0;

    std::ios_base::seekdir dir = std::ios_base::cur;
    if (whence == "set") dir = std::ios_base::beg;
    else if (whence == "end") dir = std::ios_base::end;

    file_stream.clear();
    file_stream.seekg(offset, dir);
    file_stream.seekp(offset, dir);

    if (file_stream.fail()) return {std::monostate{}};
    return {static_cast<long long>(file_stream.tellg())};
}

std::vector<LuaValue> LuaFile::write(std::shared_ptr<LuaObject> args) {
    if (is_closed) return {std::monostate{}, "attempt to use a closed file"};

    // Lua: file:write(a, b, c) -> C++ args are { "1": file, "2": a, "3": b, "4": c }
    // So we iterate from index 2.
    for (int i = 2; ; ++i) {
        auto key = std::to_string(i);
        if (!args->properties.count(key)) break;
        file_stream << to_cpp_string(args->get(key));
    }
    return {shared_from_this()}; // Return the file handle
}

std::vector<LuaValue> LuaFile::lines(std::shared_ptr<LuaObject> args) {
    if (is_closed) return {std::monostate{}, "attempt to use a closed file"};

    // The iterator function for file:lines()
    auto iterator_func = std::make_shared<LuaFunctionWrapper>([self_obj = shared_from_this()](auto) -> std::vector<LuaValue> {
        std::string line;
        // Cast self_obj to LuaFile to access file_stream
        if (auto self = std::dynamic_pointer_cast<LuaFile>(self_obj)) {
            if (std::getline(self->file_stream, line)) {
                return {line};
            }
        }
        return {std::monostate{}}; // End of iteration by returning nil
    });

    // Lua's for-loop iterator protocol returns: iterator_function, state, initial_value
    return { iterator_func, shared_from_this(), std::monostate{} };
}

// --- Global `io` Library Functions ---

std::vector<LuaValue> io_open(std::shared_ptr<LuaObject> args) {
    std::string filename = to_cpp_string(args->get("1"));
    std::string mode = args->properties.count("2") ? to_cpp_string(args->get("2")) : "r";

    auto file_obj = std::make_shared<LuaFile>(filename, mode);

    if (file_obj->is_closed) {
        return {std::monostate{}, "cannot open file '" + filename + "'"};
    }
    // Attach the shared metatable to the new LuaFile instance
    file_obj->set_metatable(file_metatable);
    return {file_obj};
}

std::vector<LuaValue> io_type(std::shared_ptr<LuaObject> args) {
    LuaValue val = args->get("1");
    if (std::holds_alternative<std::shared_ptr<LuaObject>>(val)) {
        auto obj = std::get<std::shared_ptr<LuaObject>>(val);
        if (auto file_handle = std::dynamic_pointer_cast<LuaFile>(obj)) {
            return {file_handle->is_closed ? "closed file" : "file"};
        }
    }
    return {std::monostate{}}; // nil for non-file objects
}

std::vector<LuaValue> io_input(std::shared_ptr<LuaObject> args) {
    LuaValue arg = args->get("1");
    if (std::holds_alternative<std::monostate>(arg)) {
        return {current_input_file};
    } else if (std::holds_alternative<std::string>(arg)) {
        auto open_args = std::make_shared<LuaObject>();
        open_args->set("1", arg);
        open_args->set("2", "r");
        auto new_file_vec = io_open(open_args);
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

std::vector<LuaValue> io_output(std::shared_ptr<LuaObject> args) {
    LuaValue arg = args->get("1");
    if (std::holds_alternative<std::monostate>(arg)) {
        return {current_output_file};
    } else if (std::holds_alternative<std::string>(arg)) {
        auto open_args = std::make_shared<LuaObject>();
        open_args->set("1", arg);
        open_args->set("2", "w");
        auto new_file_vec = io_open(open_args);
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

std::vector<LuaValue> io_close(std::shared_ptr<LuaObject> args) {
    LuaValue file_val = args->properties.count("1") ? args->get("1") : current_output_file;
    if (auto file_obj = get_object(file_val)) {
        auto close_func_val = file_obj->get("close");
        if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(close_func_val)) {
            auto close_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(close_func_val);
            return close_func->func(std::make_shared<LuaObject>());
        }
    }
    return {std::monostate{}, "invalid file handle"};
}

std::vector<LuaValue> io_read(std::shared_ptr<LuaObject> args) {
    auto read_func_val = current_input_file->get("read");
    if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(read_func_val)) {
        auto read_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(read_func_val);
        return read_func->func(args);
    }
    return {std::monostate{}, "input file is not readable"};
}

std::vector<LuaValue> io_write(std::shared_ptr<LuaObject> args) {
    auto write_func_val = current_output_file->get("write");
    if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(write_func_val)) {
        auto write_func = std::get<std::shared_ptr<LuaFunctionWrapper>>(write_func_val);
        return write_func->func(args);
    }
    return {std::monostate{}, "output file is not writable"};
}

// --- Library Creation ---

std::shared_ptr<LuaObject> create_io_library() {
    auto io_lib = std::make_shared<LuaObject>();

    // Create a shared metatable for LuaFile objects
    static std::shared_ptr<LuaObject> file_metatable = std::make_shared<LuaObject>();

    // Helper to create method wrappers for LuaFile
    auto make_file_method = [](auto method_ptr) {
        return std::make_shared<LuaFunctionWrapper>([method_ptr](auto args) {
            // The first argument (args->get("1")) will be the LuaFile object itself
            if (auto self_obj = get_object(args->get("1"))) {
                if (auto self = std::dynamic_pointer_cast<LuaFile>(self_obj)) {
                    return (self.get()->*method_ptr)(args);
                }
            }
            return std::vector<LuaValue>{std::monostate{}, "attempt to call method on a non-file object"};
        });
    };

    file_metatable->set("close", make_file_method(&LuaFile::close));
    file_metatable->set("flush", make_file_method(&LuaFile::flush));
    file_metatable->set("read",  make_file_method(&LuaFile::read));
    file_metatable->set("seek",  make_file_method(&LuaFile::seek));
    file_metatable->set("write", make_file_method(&LuaFile::write));
    file_metatable->set("lines", make_file_method(&LuaFile::lines));
    file_metatable->set("__index", file_metatable); // Set __index to itself for method lookup

    io_lib->set("close", std::make_shared<LuaFunctionWrapper>(io_close));
    io_lib->set("input", std::make_shared<LuaFunctionWrapper>(io_input));
    io_lib->set("open", std::make_shared<LuaFunctionWrapper>(io_open));
    io_lib->set("output", std::make_shared<LuaFunctionWrapper>(io_output));
    io_lib->set("read", std::make_shared<LuaFunctionWrapper>(io_read));
    io_lib->set("type", std::make_shared<LuaFunctionWrapper>(io_type));
    io_lib->set("write", std::make_shared<LuaFunctionWrapper>(io_write));

    // Set up standard file handles
    io_stdin_handle->set("read", std::make_shared<LuaFunctionWrapper>([](auto args) -> std::vector<LuaValue> {
        std::string line;
        if (std::getline(std::cin, line)) return {line};
        return {std::monostate{}};
    }));

    io_stdout_handle->set("write", std::make_shared<LuaFunctionWrapper>([](auto args) -> std::vector<LuaValue> {
        for (int i = 1; ; ++i) {
            auto key = std::to_string(i);
            if (!args->properties.count(key)) break;
            std::cout << to_cpp_string(args->get(key));
        }
        return {io_stdout_handle};
    }));

    io_stderr_handle->set("write", std::make_shared<LuaFunctionWrapper>([](auto args) -> std::vector<LuaValue> {
        for (int i = 1; ; ++i) {
            auto key = std::to_string(i);
            if (!args->properties.count(key)) break;
            std::cerr << to_cpp_string(args->get(key));
        }
        return {io_stderr_handle};
    }));

    io_lib->set("stdin", io_stdin_handle);
    io_lib->set("stdout", io_stdout_handle);
    io_lib->set("stderr", io_stderr_handle);

    return io_lib;
}