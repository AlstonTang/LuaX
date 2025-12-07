#include "os.hpp"
#include "lua_object.hpp"
#include <chrono>
#include <ctime>
#include <iomanip> // For std::put_time
#include <sstream> // For std::stringstream
#include <cstdlib> // For system, getenv, exit
#include <cstdio> // For remove, rename
#include <locale> // For setlocale
#include <unistd.h> // For mkstemp, close
#include <vector> // For std::vector

// os.execute
std::vector<LuaValue> os_execute(std::shared_ptr<LuaObject> args) {
	std::string command = to_cpp_string(args->get("1"));
	int result = std::system(command.c_str());
	return {static_cast<double>(result)};
}

// os.exit
std::vector<LuaValue> os_exit(std::shared_ptr<LuaObject> args) {
	double code_double = std::holds_alternative<double>(args->get("1")) ? std::get<double>(args->get("1")) : 0.0;
	bool close = std::holds_alternative<bool>(args->get("2")) ? std::get<bool>(args->get("2")) : false; // Lua 5.4 close argument

	int code = static_cast<int>(code_double);
	// In a real Lua interpreter, 'close' would handle closing the Lua state.
	// Here, we just exit the C++ program.
	std::exit(code);
	return {std::monostate{}}; // Should not be reached
}

// os.getenv
std::vector<LuaValue> os_getenv(std::shared_ptr<LuaObject> args) {
	std::string varname = to_cpp_string(args->get("1"));
	const char* value = std::getenv(varname.c_str());
	if (value) {
		return {std::string(value)};
	}
	return {std::monostate{}}; // nil
}

// os.remove
std::vector<LuaValue> os_remove(std::shared_ptr<LuaObject> args) {
	std::string filename = to_cpp_string(args->get("1"));
	if (std::remove(filename.c_str()) == 0) {
		return {true}; // Success
	}
	return {std::monostate{}}; // nil on failure
}

// os.rename
std::vector<LuaValue> os_rename(std::shared_ptr<LuaObject> args) {
	std::string oldname = to_cpp_string(args->get("1"));
	std::string newname = to_cpp_string(args->get("2"));
	if (std::rename(oldname.c_str(), newname.c_str()) == 0) {
		return {true}; // Success
	}
	return {std::monostate{}}; // nil on failure
}

// os.setlocale
std::vector<LuaValue> os_setlocale(std::shared_ptr<LuaObject> args) {
	std::string locale_str = to_cpp_string(args->get("1"));
	std::string category_str = to_cpp_string(args->get("2"));

	int category = LC_ALL; // Default
	if (category_str == "all") category = LC_ALL;
	else if (category_str == "collate") category = LC_COLLATE;
	else if (category_str == "ctype") category = LC_CTYPE;
	else if (category_str == "monetary") category = LC_MONETARY;
	else if (category_str == "numeric") category = LC_NUMERIC;
	else if (category_str == "time") category = LC_TIME;

	const char* result = std::setlocale(category, locale_str.c_str());
	if (result) {
		return {std::string(result)};
	}
	return {std::monostate{}}; // nil on failure
}

// os.tmpname
std::vector<LuaValue> os_tmpname(std::shared_ptr<LuaObject> args) {
	// mkstemp requires a template string like "XXXXXX"
	std::string temp_filename_template = "/tmp/luax_temp_XXXXXX";
	// mkstemp modifies the template string in place
	std::vector<char> buffer(temp_filename_template.begin(), temp_filename_template.end());
	buffer.push_back('\0'); // Null-terminate the string

	int fd = mkstemp(buffer.data());
	if (fd != -1) {
		// Close the file descriptor immediately as Lua's tmpname just returns the name
		close(fd);
		return {std::string(buffer.data())};
	}
	return {std::monostate{}}; // nil on failure
}

// os.date
std::vector<LuaValue> os_date(std::shared_ptr<LuaObject> args) {
	std::string format = std::holds_alternative<std::string>(args->get("1")) ? std::get<std::string>(args->get("1")) : "%c";
	time_t timer;
	if (std::holds_alternative<double>(args->get("2"))) {
		timer = static_cast<time_t>(std::get<double>(args->get("2")));
	} else {
		timer = std::time(nullptr);
	}

	struct tm* lt;
	if (format.rfind("!", 0) == 0) { // UTC time
		format = format.substr(1);
		lt = std::gmtime(&timer);
	} else { // Local time
		lt = std::localtime(&timer);
	}

	if (!lt) return {std::monostate{}};

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
		return {date_table};
	} else {
		std::stringstream ss;
		ss << std::put_time(lt, format.c_str());
		return {ss.str()};
	}
}

// os.difftime
std::vector<LuaValue> os_difftime(std::shared_ptr<LuaObject> args) {
	time_t t2 = static_cast<time_t>(get_double(args->get("1")));
	time_t t1 = static_cast<time_t>(get_double(args->get("2")));
	return {static_cast<double>(std::difftime(t2, t1))};
}

// os.clock
std::vector<LuaValue> os_clock(std::shared_ptr<LuaObject> args) {
	return {static_cast<double>(std::clock()) / CLOCKS_PER_SEC};
}

// os.time
std::vector<LuaValue> os_time(std::shared_ptr<LuaObject> args) {
	return {static_cast<double>(std::time(nullptr))};
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
