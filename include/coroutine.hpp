#ifndef COROUTINE_HPP
#define COROUTINE_HPP

#include "lua_object.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <vector>
#include <exception>

class LuaCoroutine {
public:
	enum class Status { SUSPENDED, RUNNING, DEAD };

	std::shared_ptr<LuaCallable> func;
	Status status = Status::SUSPENDED;
	std::thread worker;
	
	std::mutex mtx;
	std::condition_variable cv;

	// Memory exclusively owned and mutated by the worker thread
	LuaValueVector args;
	LuaValueVector results;
	bool error_occurred = false;
	
	// Synchronization states
	bool started = false;
	bool terminate = false;
	bool results_copied = false;

	// Handover pointers (read-only for the receiving thread)
	const LuaValue* in_args_ptr = nullptr;
	size_t in_args_size = 0;
	
	const LuaValue* out_args_ptr = nullptr;
	size_t out_args_size = 0;

	LuaCoroutine(const std::shared_ptr<LuaCallable>& f);
	~LuaCoroutine();

	void run();
	void resume(const LuaValue* args, size_t n_args, LuaValueVector& out);
	static void yield(const LuaValue* args, size_t n_args, LuaValueVector& out);
};

extern thread_local LuaCoroutine* current_coroutine;

void coroutine_create(const LuaValue* args, size_t n_args, LuaValueVector& out);
void coroutine_resume(const LuaValue* args, size_t n_args, LuaValueVector& out);
void coroutine_yield(const LuaValue* args, size_t n_args, LuaValueVector& out);
void coroutine_status(const LuaValue* args, size_t n_args, LuaValueVector& out);
void coroutine_running(const LuaValue* args, size_t n_args, LuaValueVector& out);
void coroutine_wrap(const LuaValue* args, size_t n_args, LuaValueVector& out);

std::shared_ptr<LuaObject> create_coroutine_library();

#endif