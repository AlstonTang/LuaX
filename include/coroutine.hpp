#ifndef COROUTINE_HPP
#define COROUTINE_HPP

#include "lua_object.hpp"
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>

// Moved class definition to header to support the logic extensions
class LuaCoroutine {
public:
	enum class Status : std::uint8_t { SUSPENDED, RUNNING, DEAD };

	std::shared_ptr<LuaFunctionWrapper> func;
	Status status;
	std::thread worker;

	// synchronization
	std::mutex mtx;
	std::condition_variable cv_resume; // Main -> Worker
	std::condition_variable cv_yield; // Worker -> Main

	bool started;
	LuaValueVector args; // In-bound args
	LuaValueVector results; // Out-bound results
	bool error_occurred = false;

	// EXTENSION: Mode flag
	bool is_parallel = false;

	LuaCoroutine(const std::shared_ptr<LuaFunctionWrapper>& f, bool parallel = false);
	~LuaCoroutine();

	void run();

	// Returns immediate results for standard, empty/handle for parallel
	void resume(const LuaValue* args, size_t n_args, LuaValueVector& out);

	// New: Blocks until the specific coroutine yields/returns
	void await(LuaValueVector& out);

	static void yield(const LuaValue* args, size_t n_args, LuaValueVector& out);
};

// Thread-local pointer to currently running coroutine
extern thread_local LuaCoroutine* current_coroutine;

// Lua binding functions for the coroutine library
void coroutine_create(const LuaValue* args, size_t n_args, LuaValueVector& out);
void coroutine_create_parallel(const LuaValue* args, size_t n_args, LuaValueVector& out);
void coroutine_resume(const LuaValue* args, size_t n_args, LuaValueVector& out);
void coroutine_await(const LuaValue* args, size_t n_args, LuaValueVector& out);
void coroutine_yield(const LuaValue* args, size_t n_args, LuaValueVector& out);
void coroutine_status(const LuaValue* args, size_t n_args, LuaValueVector& out);
void coroutine_running(const LuaValue* args, size_t n_args, LuaValueVector& out);
void coroutine_close(const LuaValue* args, size_t n_args, LuaValueVector& out);
void coroutine_isyieldable(const LuaValue* args, size_t n_args, LuaValueVector& out);
void coroutine_wrap(const LuaValue* args, size_t n_args, LuaValueVector& out);

std::shared_ptr<LuaObject> create_coroutine_library();

#endif // COROUTINE_HPP
