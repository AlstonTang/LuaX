#include "os.hpp"
#include "lua_object.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdio>
#include <locale>
#include <unistd.h>
#include <vector>
#include <thread>

// os.execute
void os_execute(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string command = to_cpp_string(args[0]);
	int result = std::system(command.c_str());
	out.assign({static_cast<double>(result)});
	return;
}

// os.exit
void os_exit(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	double code_double = n_args >= 1 && std::holds_alternative<double>(args[0]) ? std::get<double>(args[0]) : 0.0;
	// bool close = n_args >= 2 && std::holds_alternative<bool>(args[1]) ? std::get<bool>(args[1]) : false;
	// Lua 5.4 close argument

	int code = 0;

	if (n_args >= 1 && std::holds_alternative<long long>(args[0])) {
		code = static_cast<int>(std::get<long long>(args[0]));
	}
	else {
		code = static_cast<int>(code_double);
	}
	// In a real Lua interpreter, 'close' would handle closing the Lua state.
	// Here, we just exit the C++ program.
	std::exit(code);
}

// os.getenv
void os_getenv(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string varname = to_cpp_string(args[0]);
	if (const char* value = std::getenv(varname.c_str())) {
		out.assign({std::string(value)});
		return;
	}
	out.assign({std::monostate{}});
	return; // nil
}

// os.remove
void os_remove(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string filename = to_cpp_string(args[0]);
	if (std::remove(filename.c_str()) == 0) {
		out.assign({true});
		return; // Success
	}
	out.assign({std::monostate{}});
	return; // nil on failure
}

// os.rename
void os_rename(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string oldname = to_cpp_string(args[0]);
	std::string newname = to_cpp_string(args[1]);
	if (std::rename(oldname.c_str(), newname.c_str()) == 0) {
		out.assign({true});
		return; // Success
	}
	out.assign({std::monostate{}});
	return; // nil on failure
}

// os.setlocale
void os_setlocale(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string locale_str = to_cpp_string(args[0]);
	std::string category_str = to_cpp_string(args[1]);

	int category = LC_ALL; // Default
	if (category_str == "all") category = LC_ALL;
	else if (category_str == "collate") category = LC_COLLATE;
	else if (category_str == "ctype") category = LC_CTYPE;
	else if (category_str == "monetary") category = LC_MONETARY;
	else if (category_str == "numeric") category = LC_NUMERIC;
	else if (category_str == "time") category = LC_TIME;

	if (const char* result = std::setlocale(category, locale_str.c_str())) {
		out.assign({std::string(result)});
		return;
	}
	out.assign({std::monostate{}});
}

// os.tmpname
void os_tmpname(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	// mkstemp requires a template string like "XXXXXX"
	std::string temp_filename_template = "/tmp/luax_temp_XXXXXX";
	// mkstemp modifies the template string in place
	std::vector<char> buffer(temp_filename_template.begin(), temp_filename_template.end());
	buffer.push_back('\0'); // Null-terminate the string

	int fd = mkstemp(buffer.data());
	if (fd != -1) {
		// Close the file descriptor immediately as Lua's tmpname just returns the name
		close(fd);
		out.assign({std::string(buffer.data())});
		return;
	}
	out.assign({std::monostate{}});
	return; // nil on failure
}

// os.date
void os_date(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	std::string format = n_args >= 1 && std::holds_alternative<std::string>(args[0])
		                     ? std::get<std::string>(args[0])
		                     : "%c";
	time_t timer;
	if (n_args >= 2 && std::holds_alternative<double>(args[1])) {
		timer = static_cast<time_t>(std::get<double>(args[1]));
	}
	else {
		timer = std::time(nullptr);
	}

	struct tm* lt;
	if (format.rfind("!", 0) == 0) { // UTC time
		format = format.substr(1);
		lt = std::gmtime(&timer);
	}
	else { // Local time
		lt = std::localtime(&timer);
	}

	if (!lt) {
		out.assign({std::monostate{}});
		return;
	};

	if (format == "*t") {
		auto date_table = std::make_shared<LuaObject>();
		date_table->set("year", static_cast<double>(lt->tm_year + 1900));
		date_table->set("month", static_cast<double>(lt->tm_mon + 1));
		date_table->set("day", static_cast<double>(lt->tm_mday));
		date_table->set("hour", static_cast<double>(lt->tm_hour));
		date_table->set("min", static_cast<double>(lt->tm_min));
		date_table->set("sec", static_cast<double>(lt->tm_sec));
		date_table->set("wday", static_cast<double>(lt->tm_wday + 1));
		date_table->set("yday", static_cast<double>(lt->tm_yday + 1));
		date_table->set("isdst", static_cast<bool>(lt->tm_isdst));
		out.assign({date_table});
		return;
	}
	else {
		std::stringstream ss;
		ss << std::put_time(lt, format.c_str());
		out.assign({ss.str()});
		return;
	}
}

// os.difftime
void os_difftime(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	time_t t2 = static_cast<time_t>(get_double(args[0]));
	time_t t1 = static_cast<time_t>(get_double(args[1]));
	out.assign({static_cast<double>(std::difftime(t2, t1))});
	return;
}

// os.clock
void os_clock(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({static_cast<double>(std::clock()) / CLOCKS_PER_SEC});
	return;
}

// os.time
void os_time(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({static_cast<double>(std::time(nullptr))});
	return;
}

// os.sleep
void os_sleep(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	auto duration = std::chrono::duration<double>(get_double(args[0]));
	std::this_thread::sleep_for(duration);
	out.assign({std::monostate{}});
	return;
}

std::shared_ptr<LuaObject> create_os_library() {
	static std::shared_ptr<LuaObject> os_lib;
	if (os_lib) return os_lib;

	os_lib = std::make_shared<LuaObject>();

	os_lib->properties = {
		{"clock", std::make_shared<LuaFunctionWrapper>(os_clock)},
		{"date", std::make_shared<LuaFunctionWrapper>(os_date)},
		{"difftime", std::make_shared<LuaFunctionWrapper>(os_difftime)},
		{"execute", std::make_shared<LuaFunctionWrapper>(os_execute)},
		{"exit", std::make_shared<LuaFunctionWrapper>(os_exit)},
		{"getenv", std::make_shared<LuaFunctionWrapper>(os_getenv)},
		{"remove", std::make_shared<LuaFunctionWrapper>(os_remove)},
		{"rename", std::make_shared<LuaFunctionWrapper>(os_rename)},
		{"setlocale", std::make_shared<LuaFunctionWrapper>(os_setlocale)},
		{"time", std::make_shared<LuaFunctionWrapper>(os_time)},
		{"tmpname", std::make_shared<LuaFunctionWrapper>(os_tmpname)}
	};

	return os_lib;
}
