#include "coroutine.hpp"
#include <iostream>

// Global pointer to the currently running coroutine
// thread_local ensures every thread (Main or Worker) knows its own active coroutine.
thread_local LuaCoroutine* current_coroutine = nullptr;

LuaCoroutine::LuaCoroutine(const std::shared_ptr<LuaFunctionWrapper>& f, bool parallel)
	: func(f),
	  status(Status::SUSPENDED),
	  started(false),
	  is_parallel(parallel) {
	// Launch the thread immediately, it will wait on cv_resume
	worker = std::thread(&LuaCoroutine::run, this);
}

LuaCoroutine::~LuaCoroutine() {
	{
		std::unique_lock<std::mutex> lock(mtx);
		if (status != Status::DEAD) {
			// Detach allows the thread to die naturally or daemonize, 
			// preventing std::terminate from the destructor.
			worker.detach();
			return;
		}
	}
	if (worker.joinable()) {
		worker.join();
	}
}

void LuaCoroutine::run() {
	// 1. Wait for the first resume call (start)
	{
		std::unique_lock<std::mutex> lock(mtx);
		cv_resume.wait(lock, [this] { return started; });
	}

	// 2. Execution Phase
	try {
		current_coroutine = this;

		// THREAD SAFETY:
		// We create 'execution_results' on THIS thread's stack.
		// No other thread can see this memory.
		std::vector<LuaValue> execution_results;

		// Optimization: Reserve some space to avoid immediate heap alloc
		execution_results.reserve(4);

		// Call the Lua function using the Output Parameter signature
		// Note: We use args.data() which was populated by resume() before we woke up.
		func->func(args.data(), args.size(), execution_results);

		// 3. Completion Phase
		{
			std::lock_guard<std::mutex> lock(mtx);
			results = std::move(execution_results); // Move, don't copy
			status = Status::DEAD;
		}
	}
	catch (const std::exception& e) {
		std::lock_guard<std::mutex> lock(mtx);
		results = {LuaValue(false), LuaValue(std::string(e.what()))};
		status = Status::DEAD;
		error_occurred = true;
	}
	catch (...) {
		std::lock_guard<std::mutex> lock(mtx);
		results = {LuaValue(false), LuaValue("unknown error")};
		status = Status::DEAD;
		error_occurred = true;
	}

	current_coroutine = nullptr;

	// Notify any thread waiting on await() or resume()
	cv_yield.notify_all();
}

// Updated: Returns void, takes output parameter
void LuaCoroutine::resume(const LuaValue* resume_args, size_t n_resume_args, std::vector<LuaValue>& out) {
	std::unique_lock<std::mutex> lock(mtx);

	if (status == Status::DEAD) {
		out.assign({LuaValue(false), LuaValue("cannot resume dead coroutine")});
		return;
	}
	if (status == Status::RUNNING) {
		out.assign({LuaValue(false), LuaValue("cannot resume running coroutine")});
		return;
	}

	// Copy inputs to the coroutine's internal storage
	args.assign(resume_args, resume_args + n_resume_args);

	// State transition
	if (!started) started = true;
	status = Status::RUNNING;

	// Wake up the worker thread
	cv_resume.notify_one();

	// PARALLEL: Return immediately.
	if (is_parallel) {
		out.assign({LuaValue(true), LuaValue("async_running")});
		return;
	}

	// SYNCHRONOUS: Wait for the coroutine to Yield or Die.
	cv_yield.wait(lock, [this] { return status != Status::RUNNING; });

	// Populate Output
	if (error_occurred) {
		out = results;
	}
	else {
		out.reserve(results.size() + 1);
		out.assign({LuaValue(true)});
		out.insert(out.end(), results.begin(), results.end());
	}
}

void LuaCoroutine::await(std::vector<LuaValue>& out) {
	std::unique_lock<std::mutex> lock(mtx);

	if (!started) {
		out.assign({LuaValue(false), LuaValue("coroutine has not been started")});
		return;
	}

	// Wait for death or yield
	cv_yield.wait(lock, [this] { return status != Status::RUNNING; });

	if (error_occurred) {
		out = results;
	}
	else {
		out.reserve(results.size() + 1);
		out.assign({LuaValue(true)});
		out.insert(out.end(), results.begin(), results.end());
	}
}

// Updated: Takes output parameter
void LuaCoroutine::yield(const LuaValue* yield_args, size_t n_args, std::vector<LuaValue>& out) {
	if (!current_coroutine) {
		throw std::runtime_error("attempt to yield from outside a coroutine");
	}

	LuaCoroutine* self = current_coroutine;
	std::unique_lock<std::mutex> lock(self->mtx);

	// Copy yield arguments to member for the main thread to pick up
	self->results.assign(yield_args, yield_args + n_args);
	self->status = Status::SUSPENDED;

	self->cv_yield.notify_all(); // Wake up main thread (inside resume/await)

	// Wait until main thread calls resume() again
	self->cv_resume.wait(lock, [self] { return self->status == Status::RUNNING; });

	// When we wake up, 'self->args' contains the values passed to resume()
	out = self->args;
}

// --- Lua Bindings (Updated for Void/Out Architecture) ---

void coroutine_create(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args > 0 && std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(args[0])) {
		auto func = std::get<std::shared_ptr<LuaFunctionWrapper>>(args[0]);
		auto co = std::make_shared<LuaCoroutine>(func, false);
		out.push_back(LuaValue(co));
		return;
	}
	throw std::runtime_error("bad argument #1 to 'create'");
}

void coroutine_create_parallel(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args > 0 && std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(args[0])) {
		auto func = std::get<std::shared_ptr<LuaFunctionWrapper>>(args[0]);
		auto co = std::make_shared<LuaCoroutine>(func, true);
		out.push_back(LuaValue(co));
		return;
	}
	throw std::runtime_error("bad argument #1 to 'create_parallel'");
}

void coroutine_resume(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args > 0 && std::holds_alternative<std::shared_ptr<LuaCoroutine>>(args[0])) {
		auto co = std::get<std::shared_ptr<LuaCoroutine>>(args[0]);
		// Pass remainder of arguments and the output buffer directly
		if (n_args > 1) {
			co->resume(args + 1, n_args - 1, out);
		}
		else {
			co->resume(nullptr, 0, out);
		}
		return;
	}
	throw std::runtime_error("bad argument #1 to 'resume'");
}

void coroutine_await(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args > 0 && std::holds_alternative<std::shared_ptr<LuaCoroutine>>(args[0])) {
		auto co = std::get<std::shared_ptr<LuaCoroutine>>(args[0]);
		co->await(out); // Pass buffer directly
		return;
	}
	throw std::runtime_error("bad argument #1 to 'await' (thread expected)");
}

void coroutine_yield(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	// Call static yield, passing the output buffer
	LuaCoroutine::yield(args, n_args, out);
}

void coroutine_status(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	if (n_args > 0 && std::holds_alternative<std::shared_ptr<LuaCoroutine>>(args[0])) {
		auto co = std::get<std::shared_ptr<LuaCoroutine>>(args[0]);
		switch (co->status) {
		case LuaCoroutine::Status::SUSPENDED: out.push_back(LuaValue("suspended"));
			return;
		case LuaCoroutine::Status::RUNNING: out.push_back(LuaValue("running"));
			return;
		case LuaCoroutine::Status::DEAD: out.push_back(LuaValue("dead"));
			return;
		}
	}
	out.push_back(LuaValue("invalid"));
}

void coroutine_running(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({std::monostate{}, (current_coroutine) ? false : true});
}

void coroutine_close(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({LuaValue(true)});
}

void coroutine_isyieldable(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	out.assign({LuaValue(current_coroutine != nullptr)});
}

void coroutine_wrap(const LuaValue* args, size_t n_args, std::vector<LuaValue>& out) {
	// 1. Create the coroutine
	std::vector<LuaValue> co_res;
	coroutine_create(args, n_args, co_res);

	if (co_res.empty() || !std::holds_alternative<std::shared_ptr<LuaCoroutine>>(co_res[0])) {
		throw std::runtime_error("coroutine.wrap failed to create coroutine");
	}
	auto co = std::get<std::shared_ptr<LuaCoroutine>>(co_res[0]);

	// 2. Return a wrapper function
	auto wrapper = std::make_shared<LuaFunctionWrapper>(
		[co](const LuaValue* wrap_args, size_t n_wrap_args, std::vector<LuaValue>& wrap_out) {
			wrap_out.clear();
			co->resume(wrap_args, n_wrap_args, wrap_out);

			if (wrap_out.empty()) return; // Should not happen based on resume logic

			// Check success bool
			bool success = false;
			if (std::holds_alternative<bool>(wrap_out[0])) {
				success = std::get<bool>(wrap_out[0]); // Check bool value
			}
			else if (std::holds_alternative<long long>(wrap_out[0])) {
				success = std::get<long long>(wrap_out[0]) != 0; // Robustness
			}

			if (!success) {
				// Error!
				std::string err = "unknown error";
				if (wrap_out.size() > 1) err = to_cpp_string(wrap_out[1]);
				throw std::runtime_error(err);
			}

			// Remove the first element (the boolean 'true')
			// Vector erase is O(N) but N is usually small here.
			wrap_out.erase(wrap_out.begin());
		});
	out.assign({LuaValue(wrapper)});
}

std::shared_ptr<LuaObject> create_coroutine_library() {
	static std::shared_ptr<LuaObject> coroutine_lib;
	if (coroutine_lib) return coroutine_lib;

	coroutine_lib = std::make_shared<LuaObject>();

	coroutine_lib->properties = {
		{"await", std::make_shared<LuaFunctionWrapper>(coroutine_await)},
		{"close", std::make_shared<LuaFunctionWrapper>(coroutine_close)},
		{"create", std::make_shared<LuaFunctionWrapper>(coroutine_create)},
		{"create_parallel", std::make_shared<LuaFunctionWrapper>(coroutine_create_parallel)},
		{"isyieldable", std::make_shared<LuaFunctionWrapper>(coroutine_isyieldable)},
		{"resume", std::make_shared<LuaFunctionWrapper>(coroutine_resume)},
		{"running", std::make_shared<LuaFunctionWrapper>(coroutine_running)},
		{"status", std::make_shared<LuaFunctionWrapper>(coroutine_status)},
		{"wrap", std::make_shared<LuaFunctionWrapper>(coroutine_wrap)},
		{"yield", std::make_shared<LuaFunctionWrapper>(coroutine_yield)},
	};

	return coroutine_lib;
}
