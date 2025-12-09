#ifndef COROUTINE_HPP
#define COROUTINE_HPP

#include "lua_object.hpp"
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>

// Moved class definition to header to support the logic extensions
class LuaCoroutine {
public:
	enum class Status { SUSPENDED, RUNNING, DEAD };

	std::shared_ptr<LuaFunctionWrapper> func;
	Status status;
	std::thread worker;
	
	// synchronization
	std::mutex mtx;
	std::condition_variable cv_resume; // Main -> Worker
	std::condition_variable cv_yield;  // Worker -> Main

	bool started;
	std::vector<LuaValue> args;    // In-bound args
	std::vector<LuaValue> results; // Out-bound results
	bool error_occurred = false;
	
	// EXTENSION: Mode flag
	bool is_parallel = false; 

	LuaCoroutine(std::shared_ptr<LuaFunctionWrapper> f, bool parallel = false);
	~LuaCoroutine();

	void run();
	
	// Returns immediate results for standard, empty/handle for parallel
	std::vector<LuaValue> resume(std::vector<LuaValue> resume_args);
	
	// New: Blocks until the specific coroutine yields/returns
	std::vector<LuaValue> await(); 

	static std::vector<LuaValue> yield(std::vector<LuaValue> yield_args);
};

std::shared_ptr<LuaObject> create_coroutine_library();

#endif // COROUTINE_HPP