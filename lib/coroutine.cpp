#include "coroutine.hpp"
#include <stdexcept>

thread_local LuaCoroutine* current_coroutine = nullptr;

// Internal exception used to safely unwind the C++ stack 
// if the coroutine is garbage-collected while suspended.
struct CoroutineTerminated : public std::exception {};

LuaCoroutine::LuaCoroutine(const std::shared_ptr<LuaCallable>& f) : func(f) {
	// Start the persistent worker thread
	worker = std::thread(&LuaCoroutine::run, this);
}

LuaCoroutine::~LuaCoroutine() {
	// Signal the worker thread to terminate and wake it up
	{
		std::lock_guard<std::mutex> lock(mtx);
		terminate = true;
	}
	cv.notify_all();
	
	// Wait for the worker thread to safely destroy its own memory and exit
	if (worker.joinable()) {
		worker.join();
	}
}

void LuaCoroutine::run() {
	while (true) {
		{
			std::unique_lock<std::mutex> lock(mtx);
			cv.wait(lock, [this] { return (started && status == Status::RUNNING) || terminate; });
			if (terminate) break;

			// Safely copy inbound arguments onto the Worker Thread's allocator
			args.assign(in_args_ptr, in_args_ptr + in_args_size);
		}

		try {
			current_coroutine = this;
			LuaValueVector exec_results;
			
			// Execute the Lua function
			func->call(args.data(), args.size(), exec_results);

			// Normal completion
			std::unique_lock<std::mutex> lock(mtx);
			results = std::move(exec_results);
			out_args_ptr = results.data();
			out_args_size = results.size();
			status = Status::DEAD;
			results_copied = false;
		}
		catch (const CoroutineTerminated&) {
			// Thread was GC'd by Lua while yielding; unwind stack cleanly
			std::unique_lock<std::mutex> lock(mtx);
			status = Status::DEAD;
			out_args_size = 0;
			results_copied = false;
		}
		catch (const std::exception& e) {
			std::unique_lock<std::mutex> lock(mtx);
			results = { LuaValue(std::string(e.what())) };
			out_args_ptr = results.data();
			out_args_size = results.size();
			status = Status::DEAD;
			error_occurred = true;
			results_copied = false;
		}
		catch (...) {
			std::unique_lock<std::mutex> lock(mtx);
			results = { LuaValue(std::string_view("unknown error")) };
			out_args_ptr = results.data();
			out_args_size = results.size();
			status = Status::DEAD;
			error_occurred = true;
			results_copied = false;
		}

		current_coroutine = nullptr;

		{
			std::unique_lock<std::mutex> lock(mtx);
			cv.notify_all(); // Wake up the caller of resume()
			
			// Wait until the Main Thread has finished copying the results
			cv.wait(lock, [this] { return results_copied || terminate; });
			
			// Safely clear memory allocated by THIS thread
			results.clear();
			args.clear();

			if (status == Status::DEAD) {
				// Wait for ~LuaCoroutine to set terminate
				cv.wait(lock, [this] { return terminate; });
				break;
			}
		}
	}

	// ==========================================
	// CRITICAL LIFETIME FIX FOR NESTED COROUTINES
	// ==========================================
	std::shared_ptr<LuaCallable> local_func;
	{
		std::lock_guard<std::mutex> lock(mtx);
		args.clear();
		results.clear();
		local_func = std::move(func); // Take ownership of the function
	}
	
	// Destroy the function environment (which contains nested coroutines) 
	// ON THE THREAD THAT CREATED THEM. This prevents cross-thread frees 
	// from triggering "unaligned tcache chunk" crashes.
	local_func.reset();

	// Safely flush the pool now that all LuaValues on this thread are dead
	luax_flush_thread_pool();
}

void LuaCoroutine::resume(const LuaValue* resume_args, size_t n_resume_args, LuaValueVector& out) {
	std::unique_lock<std::mutex> lock(mtx);

	if (status == Status::DEAD) {
		out.assign({ LuaValue(false), LuaValue(std::string_view("cannot resume dead coroutine")) });
		return;
	}

	// Provide pointers for the worker thread to copy
	in_args_ptr = resume_args;
	in_args_size = n_resume_args;
	status = Status::RUNNING;
	started = true;

	cv.notify_all();

	// Wait for the worker thread to suspend or die
	cv.wait(lock, [this] { return status != Status::RUNNING; });

	// Deep copy the results to the Main Thread's pool
	try {
		if (error_occurred) {
			out.assign({ LuaValue(false), out_args_size ? out_args_ptr[0] : LuaValue() });
		} else {
			out.reserve(out_args_size + 1);
			out.assign({ LuaValue(true) });
			out.insert(out.end(), out_args_ptr, out_args_ptr + out_args_size);
		}
	} catch (...) {
		results_copied = true;
		cv.notify_all();
		throw;
	}

	// Tell the worker it can now safely clear its results buffer
	results_copied = true;
	cv.notify_all();
}

void LuaCoroutine::yield(const LuaValue* yield_args, size_t n_args, LuaValueVector& out) {
	LuaCoroutine* self = current_coroutine;
	if (!self) throw std::runtime_error("attempt to yield from outside a coroutine");

	std::unique_lock<std::mutex> lock(self->mtx);
	
	// Allocate results on the Worker Thread
	self->results.assign(yield_args, yield_args + n_args);
	self->out_args_ptr = self->results.data();
	self->out_args_size = self->results.size();
	self->status = Status::SUSPENDED;
	self->results_copied = false;

	self->cv.notify_all(); // Wake up resume() caller

	// Wait until the coroutine is resumed again
	self->cv.wait(lock, [self] { return self->status == Status::RUNNING || self->terminate; });

	// If garbage collected while waiting, unwind the C++ stack gracefully
	if (self->terminate) {
		throw CoroutineTerminated();
	}

	// Waking up from resume! Deep copy new inbound arguments
	self->args.assign(self->in_args_ptr, self->in_args_ptr + self->in_args_size);
	out = self->args; 
	self->results.clear(); // Safely clear old yield results
}

// --- Bindings ---

void coroutine_create(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args > 0 && std::holds_alternative<std::shared_ptr<LuaCallable>>(args[0])) {
		out.push_back(LuaValue(std::make_shared<LuaCoroutine>(std::get<std::shared_ptr<LuaCallable>>(args[0]))));
		return;
	}
	throw std::runtime_error("function expected");
}

void coroutine_resume(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args > 0 && std::holds_alternative<std::shared_ptr<LuaCoroutine>>(args[0])) {
		auto co = std::get<std::shared_ptr<LuaCoroutine>>(args[0]);
		co->resume(n_args > 1 ? args + 1 : nullptr, n_args > 1 ? n_args - 1 : 0, out);
		return;
	}
	throw std::runtime_error("thread expected");
}

void coroutine_yield(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	LuaCoroutine::yield(args, n_args, out);
}

void coroutine_status(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	if (n_args > 0 && std::holds_alternative<std::shared_ptr<LuaCoroutine>>(args[0])) {
		auto co = std::get<std::shared_ptr<LuaCoroutine>>(args[0]);
		const char* s = (co->status == LuaCoroutine::Status::DEAD) ? "dead" : "suspended";
		out.push_back(LuaValue(std::string_view(s)));
		return;
	}
	out.push_back(LuaValue(std::string_view("invalid")));
}

void coroutine_running(const LuaValue*, size_t, LuaValueVector& out) {
	out.assign({ std::monostate{}, (current_coroutine == nullptr) });
}

void coroutine_wrap(const LuaValue* args, size_t n_args, LuaValueVector& out) {
	LuaValueVector res;
	coroutine_create(args, n_args, res);
	auto co = std::get<std::shared_ptr<LuaCoroutine>>(res[0]);

	out.push_back(LuaValue(make_lua_callable([co](const LuaValue* a, size_t n, LuaValueVector& o) {
		LuaValueVector w_out;
		co->resume(a, n, w_out);
		if (w_out.empty()) return;
		
		if (std::holds_alternative<bool>(w_out[0]) && !std::get<bool>(w_out[0])) {
			throw std::runtime_error(w_out.size() > 1 ? to_cpp_string(w_out[1]) : "unknown error");
		}
		w_out.erase(w_out.begin());
		o = std::move(w_out);
	})));
}

std::shared_ptr<LuaObject> create_coroutine_library() {
	auto lib = std::make_shared<LuaObject>();
	lib->set("create", LUA_C_FUNC(coroutine_create));
	lib->set("resume", LUA_C_FUNC(coroutine_resume));
	lib->set("yield", LUA_C_FUNC(coroutine_yield));
	lib->set("status", LUA_C_FUNC(coroutine_status));
	lib->set("running", LUA_C_FUNC(coroutine_running));
	lib->set("wrap", LUA_C_FUNC(coroutine_wrap));
	return lib;
}