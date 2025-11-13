#include "io.hpp"
#include "lua_object.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <memory>
#include <cstdio> // For remove (used by LuaFile destructor implicitly)
#include <vector> // For LuaFile::read buffer and mkstemp buffer
#include <unistd.h> // For mkstemp, close

// Global file handles for stdin, stdout, stderr
std::shared_ptr<LuaObject> io_stdin_handle = std::make_shared<LuaObject>();
std::shared_ptr<LuaObject> io_stdout_handle = std::make_shared<LuaObject>();
std::shared_ptr<LuaObject> io_stderr_handle = std::make_shared<LuaObject>();

std::shared_ptr<LuaObject> current_input_file = io_stdin_handle;
std::shared_ptr<LuaObject> current_output_file = io_stdout_handle;

// LuaFile constructor
LuaFile::LuaFile(const std::string& filename, const std::string& mode) {
    std::ios_base::openmode open_mode = std::ios_base::in; // Default to read
    if (mode.find("w") != std::string::npos) {
        open_mode = std::ios_base::out | std::ios_base::trunc;
    } else if (mode.find("a") != std::string::npos) {
        open_mode = std::ios_base::out | std::ios_base::app;
    } else if (mode.find("r+") != std::string::npos) {
        open_mode = std::ios_base::in | std::ios_base::out;
    } else if (mode.find("w+") != std::string::npos) {
        open_mode = std::ios_base::in | std::ios_base::out | std::ios_base::trunc;
    } else if (mode.find("a+") != std::string::npos) {
        open_mode = std::ios_base::in | std::ios_base::out | std::ios_base::app;
    }

    if (mode.find("b") != std::string::npos) {
        open_mode |= std::ios_base::binary;
    }

    file_stream.open(filename, open_mode);
    is_closed = !file_stream.is_open();
}

// LuaFile destructor
LuaFile::~LuaFile() {
    if (file_stream.is_open()) {
        file_stream.close();
    }
}

// LuaFile::close
std::vector<LuaValue> LuaFile::close(std::shared_ptr<LuaObject> args) {
    if (file_stream.is_open()) {
        file_stream.close();
        is_closed = true;
    }
    return {true}; // Lua returns true on success
}

// LuaFile::flush
std::vector<LuaValue> LuaFile::flush(std::shared_ptr<LuaObject> args) {
    if (file_stream.is_open()) {
        file_stream.flush();
        return {true}; // Lua returns true on success
    }
    return {std::monostate{}}; // nil on failure
}

// LuaFile::read
std::vector<LuaValue> LuaFile::read(std::shared_ptr<LuaObject> args) {
    if (!file_stream.is_open() || is_closed) {
        return {std::monostate{}}; // nil if file is closed
    }

    std::string format = "*l"; // Default to read line
    if (args->properties.count("1")) {
        format = std::get<std::string>(args->get("1"));
    }

    if (format == "*l") {
        std::string line;
        if (std::getline(file_stream, line)) {
            return {line};
        }
    } else if (format == "*a") {
        std::string content((std::istreambuf_iterator<char>(file_stream)),
                            std::istreambuf_iterator<char>());
        return {content};
    } else if (format == "*n") {
        double num;
        file_stream >> num;
        if (file_stream.fail()) {
            file_stream.clear(); // Clear error flags
            return {std::monostate{}}; // nil if read fails
        }
        return {num};
    } else { // Read N bytes
        try {
            int num_bytes = static_cast<int>(std::stod(format));
            std::vector<char> buffer(num_bytes);
            file_stream.read(buffer.data(), num_bytes);
            if (file_stream.gcount() > 0) {
                return {std::string(buffer.data(), file_stream.gcount())};
            }
        } catch (...) {
            // Invalid format, return nil
        }
    }
    return {std::monostate{}}; // nil on failure or EOF
}

// LuaFile::seek
std::vector<LuaValue> LuaFile::seek(std::shared_ptr<LuaObject> args) {
    if (!file_stream.is_open() || is_closed) {
        return {std::monostate{}}; // nil if file is closed
    }

    std::string whence = std::holds_alternative<std::string>(args->get("1")) ? std::get<std::string>(args->get("1")) : "cur";
    long long offset = std::holds_alternative<double>(args->get("2")) ? static_cast<long long>(std::get<double>(args->get("2"))) : 0;

    std::ios_base::seekdir dir;
    if (whence == "set") {
        dir = std::ios_base::beg;
    } else if (whence == "end") {
        dir = std::ios_base::end;
    } else { // "cur"
        dir = std::ios_base::cur;
    }

    file_stream.clear(); // Clear any error flags before seeking
    file_stream.seekg(offset, dir);
    file_stream.seekp(offset, dir);

    if (file_stream.fail()) {
        return {std::monostate{}}; // nil on failure
    }
    return {static_cast<double>(file_stream.tellg())}; // Return current position
}

// LuaFile::write
std::vector<LuaValue> LuaFile::write(std::shared_ptr<LuaObject> args) {
    if (!file_stream.is_open() || is_closed) {
        return {std::monostate{}}; // nil if file is closed
    }
    for (int i = 1; ; ++i) {
        LuaValue val = args->get(std::to_string(i));
        if (std::holds_alternative<std::monostate>(val)) break;
        file_stream << to_cpp_string(val);
    }
    return {shared_from_this()}; // Return file handle (now a LuaObject)
}

// io.flush (global)
std::vector<LuaValue> io_flush_global(std::shared_ptr<LuaObject> args) {
    std::cout.flush();
    return {std::monostate{}};
}

// io.open (global)
std::vector<LuaValue> io_open(std::shared_ptr<LuaObject> args) {
    std::string filename = to_cpp_string(args->get("1"));
    std::string mode = std::holds_alternative<std::string>(args->get("2")) ? std::get<std::string>(args->get("2")) : "r";

    auto file_obj = std::make_shared<LuaFile>(filename, mode);
    if (file_obj->file_stream.is_open()) {
        file_obj->set("close", std::make_shared<LuaFunctionWrapper>([file_obj](std::shared_ptr<LuaObject> f_args) -> std::vector<LuaValue> { return file_obj->close(f_args); }));
        file_obj->set("flush", std::make_shared<LuaFunctionWrapper>([file_obj](std::shared_ptr<LuaObject> f_args) -> std::vector<LuaValue> { return file_obj->flush(f_args); }));
        file_obj->set("read", std::make_shared<LuaFunctionWrapper>([file_obj](std::shared_ptr<LuaObject> f_args) -> std::vector<LuaValue> { return file_obj->read(f_args); }));
        file_obj->set("seek", std::make_shared<LuaFunctionWrapper>([file_obj](std::shared_ptr<LuaObject> f_args) -> std::vector<LuaValue> { return file_obj->seek(f_args); }));
        file_obj->set("setvbuf", std::make_shared<LuaFunctionWrapper>([file_obj](std::shared_ptr<LuaObject> f_args) -> std::vector<LuaValue> { return file_obj->setvbuf(f_args); }));
        file_obj->set("write", std::make_shared<LuaFunctionWrapper>([file_obj](std::shared_ptr<LuaObject> f_args) -> std::vector<LuaValue> { return file_obj->write(f_args); }));
        return {file_obj};
    }
    return {std::monostate{}}; // nil on failure
}

// io.write (global)
std::vector<LuaValue> io_write_global(std::shared_ptr<LuaObject> args) {
    // Use the current_output_file's write method
    if (current_output_file && std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(current_output_file->get("write"))) {
        return std::get<std::shared_ptr<LuaFunctionWrapper>>(current_output_file->get("write"))->func(args);
    }
    return {std::monostate{}};
}

// io.read (global)
std::vector<LuaValue> io_read_global(std::shared_ptr<LuaObject> args) {
    // Use the current_input_file's read method
    if (current_input_file && std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(current_input_file->get("read"))) {
        return std::get<std::shared_ptr<LuaFunctionWrapper>>(current_input_file->get("read"))->func(args);
    }
    return {std::monostate{}};
}

// io.input
std::vector<LuaValue> io_input(std::shared_ptr<LuaObject> args) {
    LuaValue arg = args->get("1");
    if (std::holds_alternative<std::monostate>(arg)) {
        // io.input() returns current input file
        return {current_input_file};
    } else if (std::holds_alternative<std::string>(arg)) {
        // io.input(filename) opens file and sets as input
        std::string filename = std::get<std::string>(arg);
        auto open_args = std::make_shared<LuaObject>();
        open_args->set("1", filename);
        open_args->set("2", "r");
        std::vector<LuaValue> new_file_handle_vec = io_open(open_args);
        if (!new_file_handle_vec.empty() && std::holds_alternative<std::shared_ptr<LuaObject>>(new_file_handle_vec[0])) {
            current_input_file = std::get<std::shared_ptr<LuaObject>>(new_file_handle_vec[0]);
            return {current_input_file};
        }
        return {std::monostate{}}; // nil on error
    } else if (std::holds_alternative<std::shared_ptr<LuaObject>>(arg)) {
        // io.input(filehandle) sets filehandle as input
        current_input_file = std::get<std::shared_ptr<LuaObject>>(arg);
        return {current_input_file};
    }
    return {std::monostate{}};
}

// io.output
std::vector<LuaValue> io_output(std::shared_ptr<LuaObject> args) {
    LuaValue arg = args->get("1");
    if (std::holds_alternative<std::monostate>(arg)) {
        // io.output() returns current output file
        return {current_output_file};
    } else if (std::holds_alternative<std::string>(arg)) {
        // io.output(filename) opens file and sets as output
        std::string filename = std::get<std::string>(arg);
        auto open_args = std::make_shared<LuaObject>();
        open_args->set("1", filename);
        open_args->set("2", "w");
        std::vector<LuaValue> new_file_handle_vec = io_open(open_args);
        if (!new_file_handle_vec.empty() && std::holds_alternative<std::shared_ptr<LuaObject>>(new_file_handle_vec[0])) {
            current_output_file = std::get<std::shared_ptr<LuaObject>>(new_file_handle_vec[0]);
            return {current_output_file};
        }
        return {std::monostate{}}; // nil on error
    } else if (std::holds_alternative<std::shared_ptr<LuaObject>>(arg)) {
        // io.output(filehandle) sets filehandle as output
        current_output_file = std::get<std::shared_ptr<LuaObject>>(arg);
        return {current_output_file};
    }
    return {std::monostate{}};
}

// io.lines (not supported in translated environment)
std::vector<LuaValue> io_lines(std::shared_ptr<LuaObject> args) {
    throw std::runtime_error("io.lines is not supported in the translated environment.");
    return {std::monostate{}}; // Should not be reached
}

// io.close (global)
std::vector<LuaValue> io_close_global(std::shared_ptr<LuaObject> args) {
    LuaValue file_val = args->get("1");
    if (std::holds_alternative<std::monostate>(file_val)) {
        // io.close() closes the default output file
        if (current_output_file && std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(current_output_file->get("close"))) {
            return std::get<std::shared_ptr<LuaFunctionWrapper>>(current_output_file->get("close"))->func(args);
        }
    } else if (std::holds_alternative<std::shared_ptr<LuaObject>>(file_val)) {
        // io.close(filehandle) closes the given filehandle
        auto file_handle = std::get<std::shared_ptr<LuaObject>>(file_val);
        if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(file_handle->get("close"))) {
            return std::get<std::shared_ptr<LuaFunctionWrapper>>(file_handle->get("close"))->func(args);
        }
    }
    return {std::monostate{}}; // nil on error
}

// io.tmpfile (global)
std::vector<LuaValue> io_tmpfile_global(std::shared_ptr<LuaObject> args) {
    // mkstemp requires a template string like "XXXXXX"
    std::string temp_filename_template = "/tmp/luax_io_temp_XXXXXX";
    // mkstemp modifies the template string in place
    std::vector<char> buffer(temp_filename_template.begin(), temp_filename_template.end());
    buffer.push_back('\0'); // Null-terminate the string

    int fd = mkstemp(buffer.data());
    if (fd != -1) {
        // mkstemp creates and opens the file. We need to close the fd,
        // but keep the file on disk for io_open to use.
        close(fd);
        std::string filename = buffer.data();
        auto open_args = std::make_shared<LuaObject>();
        open_args->set("1", filename);
        open_args->set("2", "w+"); // Read and write mode
        return io_open(open_args);
    }
    return {std::monostate{}}; // nil on failure
}

// io.type (global)
std::vector<LuaValue> io_type_global(std::shared_ptr<LuaObject> args) {
    LuaValue arg = args->get("1");
    if (std::holds_alternative<std::shared_ptr<LuaObject>>(arg)) {
        auto obj = std::get<std::shared_ptr<LuaObject>>(arg);
        // Check if it's a LuaFile object by trying to cast it
        std::shared_ptr<LuaFile> file_handle = std::dynamic_pointer_cast<LuaFile>(obj);
        if (file_handle) {
            if (!file_handle->is_closed) {
                return {"file"};
            } else {
                return {"closed file"};
            }
        }
    }
    return {std::monostate{}}; // nil
}

std::shared_ptr<LuaObject> create_io_library() {
    auto io_lib = std::make_shared<LuaObject>();

    io_lib->set("close", std::make_shared<LuaFunctionWrapper>(io_close_global)); // Use global io_close_global
    io_lib->set("flush", std::make_shared<LuaFunctionWrapper>(io_flush_global)); // Use global io_flush_global
    io_lib->set("input", std::make_shared<LuaFunctionWrapper>(io_input));
    io_lib->set("lines", std::make_shared<LuaFunctionWrapper>(io_lines));
    io_lib->set("open", std::make_shared<LuaFunctionWrapper>(io_open));
    io_lib->set("output", std::make_shared<LuaFunctionWrapper>(io_output));
    io_lib->set("read", std::make_shared<LuaFunctionWrapper>(io_read_global)); // Use global io_read_global
    io_lib->set("tmpfile", std::make_shared<LuaFunctionWrapper>(io_tmpfile_global)); // Use global io_tmpfile_global
    io_lib->set("type", std::make_shared<LuaFunctionWrapper>(io_type_global)); // Use global io_type_global
    io_lib->set("write", std::make_shared<LuaFunctionWrapper>(io_write_global)); // Use global io_write_global

    // Set up standard file handles
    io_stdin_handle->set("read", std::make_shared<LuaFunctionWrapper>([](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
        std::string format = "*l"; // Default to read line
        if (args->properties.count("1")) {
            format = std::get<std::string>(args->get("1"));
        }

        if (format == "*l") {
            std::string line;
            if (std::getline(std::cin, line)) {
                return {line};
            }
        } else if (format == "*a") {
            std::string content((std::istreambuf_iterator<char>(std::cin)),
                                std::istreambuf_iterator<char>());
            return {content};
        } else if (format == "*n") {
            double num;
            std::cin >> num;
            if (std::cin.fail()) {
                std::cin.clear(); // Clear error flags
                return {std::monostate{}}; // nil if read fails
            }
            return {num};
        }
        return {std::monostate{}}; // nil on failure or EOF
    }));

    io_stdout_handle->set("write", std::make_shared<LuaFunctionWrapper>([](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
        for (int i = 1; ; ++i) {
            LuaValue val = args->get(std::to_string(i));
            if (std::holds_alternative<std::monostate>(val)) break;
            std::cout << to_cpp_string(val);
        }
        return {io_stdout_handle}; // Return file handle
    }));
    io_stdout_handle->set("flush", std::make_shared<LuaFunctionWrapper>([](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
        std::cout.flush();
        return {io_stdout_handle}; // Return file handle
    }));

    io_stderr_handle->set("write", std::make_shared<LuaFunctionWrapper>([](std::shared_ptr<LuaObject> args) -> std::vector<LuaValue> {
        for (int i = 1; ; ++i) {
            LuaValue val = args->get(std::to_string(i));
            if (std::holds_alternative<std::monostate>(val)) break;
            std::cerr << to_cpp_string(val);
        }
        return {io_stderr_handle}; // Return file handle
    }));

    io_lib->set("stdin", io_stdin_handle);
    io_lib->set("stdout", io_stdout_handle);
    io_lib->set("stderr", io_stderr_handle);

    return io_lib;
}