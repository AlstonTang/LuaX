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
void os_execute(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	std::string command = to_cpp_string(args[0]);
	int result = std::system(command.c_str());
	out.assign({static_cast<double>(result)});
	return;
}

// os.exit
void os_exit(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	int code = 0;

	if (n_args >= 1) {
		switch (args[0].index()) {
			case INDEX_DOUBLE:
				code = static_cast<int>(args[0].get<double>());
				break;
			case INDEX_INTEGER:
				code = static_cast<int>(args[0].get<long long>());
				break;
			default:
				break;
		}
	}
	// In a real Lua interpreter, 'close' would handle closing the Lua state.
	// Here, we just exit the C++ program.
	std::exit(code);
}

// os.getenv
void os_getenv(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	std::string varname = to_cpp_string(args[0]);
	if (const char* value = std::getenv(varname.c_str())) {
		out.assign({std::string(value)});
		return;
	}
	out.assign({LuaValue()});
	return; // nil
}

// os.remove
void os_remove(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	std::string filename = to_cpp_string(args[0]);
	if (std::remove(filename.c_str()) == 0) {
		out.assign({true});
		return; // Success
	}
	out.assign({LuaValue()});
	return; // nil on failure
}

// os.rename
void os_rename(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	std::string oldname = to_cpp_string(args[0]);
	std::string newname = to_cpp_string(args[1]);
	if (std::rename(oldname.c_str(), newname.c_str()) == 0) {
		out.assign({true});
		return; // Success
	}
	out.assign({LuaValue()});
	return; // nil on failure
}

// os.setlocale
void os_setlocale(const LuaValue* args, size_t n_args, LuaValueVector& out) {
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
	out.assign({LuaValue()});
}

// os.tmpname
void os_tmpname(const LuaValue* args, size_t n_args, LuaValueVector& out) {
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
	out.assign({LuaValue()});
	return; // nil on failure
}

// os.date
void os_date(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	std::string format = "%c";
	if (n_args >= 1) {
		switch (args[0].index()) {
			case INDEX_STRING: format = args[0].get<std::string_view>(); break;
			case INDEX_STRING_VIEW: format = std::string(args[0].get<std::string_view>()); break;
			default: break;
		}
	}

	time_t timer;
	if (n_args >= 2) {
		switch (args[1].index()) {
			case INDEX_DOUBLE: timer = static_cast<time_t>(args[1].get<double>()); break;
			case INDEX_INTEGER: timer = static_cast<time_t>(args[1].get<long long>()); break;
			default: timer = std::time(nullptr); break;
		}
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
		out.assign({LuaValue()});
		return;
	};

	if (format == "*t") {
		auto date_table = new LuaObject();
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
void os_difftime(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	time_t t2 = static_cast<time_t>(get_double(args[0]));
	time_t t1 = static_cast<time_t>(get_double(args[1]));
	out.assign({static_cast<double>(std::difftime(t2, t1))});
	return;
}

// os.clock
void os_clock(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({static_cast<double>(std::clock()) / CLOCKS_PER_SEC});
	return;
}

// os.time
void os_time(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	out.assign({static_cast<double>(std::time(nullptr))});
	return;
}

// os.sleep
void os_sleep(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	auto duration = std::chrono::duration<double>(get_double(args[0]));
	std::this_thread::sleep_for(duration);
	out.assign({LuaValue()});
	return;
}

LuaObject* create_os_library() {
	static LuaObject* os_lib;
	if (os_lib) return os_lib;

	os_lib = new LuaObject();
	os_lib->set("clock", LUA_C_FUNC(os_clock));
	os_lib->set("date", LUA_C_FUNC(os_date));
	os_lib->set("difftime", LUA_C_FUNC(os_difftime));
	os_lib->set("execute", LUA_C_FUNC(os_execute));
	os_lib->set("exit", LUA_C_FUNC(os_exit));
	os_lib->set("getenv", LUA_C_FUNC(os_getenv));
	os_lib->set("remove", LUA_C_FUNC(os_remove));
	os_lib->set("rename", LUA_C_FUNC(os_rename));
	os_lib->set("setlocale", LUA_C_FUNC(os_setlocale));
	os_lib->set("time", LUA_C_FUNC(os_time));
	os_lib->set("tmpname", LUA_C_FUNC(os_tmpname));

	return os_lib;
}
