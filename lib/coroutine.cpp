#include "coroutine.hpp"
#include <iostream>

// Global pointer to the currently running coroutine
thread_local LuaCoroutine* current_coroutine = nullptr;

LuaCoroutine::LuaCoroutine(std::shared_ptr<LuaFunctionWrapper> f, bool parallel) 
	: func(f), status(Status::SUSPENDED), started(false), is_parallel(parallel) {
	worker = std::thread(&LuaCoroutine::run, this);
}

LuaCoroutine::~LuaCoroutine() {
	{
		std::unique_lock<std::mutex> lock(mtx);
		// If parallel and still running, we might need to detach or cancel
		if (status != Status::DEAD) {
			// For this implementation, we detach to avoid terminating the program,
			// though in production we should signal a stop.
			worker.detach();
			return; 
		}
	}
	if (worker.joinable()) {
		worker.join();
	}
}

void LuaCoroutine::run() {
	// Phase 1: Wait for start and capture arguments safely
	{
		std::unique_lock<std::mutex> lock(mtx);
		cv_resume.wait(lock, [this] { return started; });
		
		// MIGHT WANT TO COPY?
	} // Lock released here, allowing main thread to call await()

	// Phase 2: Execute
	try {
		current_coroutine = this;
		
		// Execute the Lua function (NO LOCK HELD, allowing concurrency)
		std::vector<LuaValue> res = func->func(args);
		
		// Phase 3: Store results
		{
			std::lock_guard<std::mutex> lock(mtx);
			results = res;
			status = Status::DEAD;
		}
	} catch (const std::exception& e) {
		std::lock_guard<std::mutex> lock(mtx);
		results = {false, std::string(e.what())}; 
		status = Status::DEAD;
		error_occurred = true;
	} catch (...) {
		std::lock_guard<std::mutex> lock(mtx);
		results = {false, "unknown error"};
		status = Status::DEAD;
		error_occurred = true;
	}

	current_coroutine = nullptr;
	
	// Notify all waiters (await or resume) that we are finished
	cv_yield.notify_all(); 
}

std::vector<LuaValue> LuaCoroutine::resume(std::vector<LuaValue> resume_args) {
	std::unique_lock<std::mutex> lock(mtx);

	if (status == Status::DEAD) return {false, "cannot resume dead coroutine"};
	if (status == Status::RUNNING) return {false, "cannot resume running coroutine"};

	args = resume_args;
	
	// State transition
	if (!started) started = true;
	status = Status::RUNNING;
	
	// Wake up the worker
	cv_resume.notify_one();

	// PARALLEL: Return immediately. 
	// The main thread will continue and eventually call await().
	if (is_parallel) {
		return {true, "async_running"}; 
	}

	// SYNCHRONOUS: Wait for result.
	cv_yield.wait(lock, [this] { return status != Status::RUNNING; });

	std::vector<LuaValue> res;
	if (error_occurred) {
		res = results;
	} else {
		res.push_back(true);
		res.insert(res.end(), results.begin(), results.end());
	}
	return res;
}

std::vector<LuaValue> LuaCoroutine::await() {
	std::unique_lock<std::mutex> lock(mtx);
	
	if (!started) {
		return {false, "coroutine has not been started"};
	}

	cv_yield.wait(lock, [this] { 
		return status != Status::RUNNING; 
	});

	// 3. Return results
	std::vector<LuaValue> res;
	if (error_occurred) {
		// results contains {false, "error message"}
		res = results;
	} else {
		// Standard Lua success format: {true, val1, val2...}
		res.push_back(true);
		res.insert(res.end(), results.begin(), results.end());
	}
	return res;
}

std::vector<LuaValue> LuaCoroutine::yield(std::vector<LuaValue> yield_args) {
	if (!current_coroutine) {
		throw std::runtime_error("attempt to yield from outside a coroutine");
	}

	LuaCoroutine* self = current_coroutine;
	std::unique_lock<std::mutex> lock(self->mtx);

	self->results = yield_args;
	self->status = Status::SUSPENDED;

	self->cv_yield.notify_all(); // Notify main thread (await or resume)

	// Wait until resumed again
	self->cv_resume.wait(lock, [self] { return self->status == Status::RUNNING; });
	
	current_coroutine = self;
	return self->args;
}

// --- Lua Bindings ---

std::vector<LuaValue> coroutine_create(std::vector<LuaValue> args) {
	LuaValue val = args.at(0);
	if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(val)) {
		auto func = std::get<std::shared_ptr<LuaFunctionWrapper>>(val);
		// Default: Standard Synchronous Coroutine
		auto co = std::make_shared<LuaCoroutine>(func, false);
		return {co};
	}
	throw std::runtime_error("bad argument #1 to 'create'");
}

// EXTENSION: Create Parallel
std::vector<LuaValue> coroutine_create_parallel(std::vector<LuaValue> args) {
	LuaValue val = args.at(0);
	if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(val)) {
		auto func = std::get<std::shared_ptr<LuaFunctionWrapper>>(val);
		// Parallel Mode
		auto co = std::make_shared<LuaCoroutine>(func, true);
		return {co};
	}
	throw std::runtime_error("bad argument #1 to 'create_parallel'");
}

std::vector<LuaValue> coroutine_resume(std::vector<LuaValue> args) {
	LuaValue val = args.at(0);
	if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(val)) {
		auto co = std::get<std::shared_ptr<LuaCoroutine>>(val);
		std::vector<LuaValue> resume_args;
		for (int i = 1; i < args.size(); ++i) {
			resume_args.push_back(args.at(i));
		}
		return co->resume(resume_args);
	}
	throw std::runtime_error("bad argument #1 to 'resume'");
}

// EXTENSION: Await Binding
std::vector<LuaValue> coroutine_await(std::vector<LuaValue> args) {
	LuaValue val = args.at(0);
	if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(val)) {
		auto co = std::get<std::shared_ptr<LuaCoroutine>>(val);
		return co->await();
	}
	throw std::runtime_error("bad argument #1 to 'await' (thread expected)");
}

// Existing yield, status, running implementations remain mostly unchanged...
std::vector<LuaValue> coroutine_yield(std::vector<LuaValue> args) {
	std::vector<LuaValue> yield_args;
	for (int i = 0; i < args.size(); ++i) {
		yield_args.push_back(args.at(i));
	}
	return LuaCoroutine::yield(yield_args);
}

std::vector<LuaValue> coroutine_status(std::vector<LuaValue> args) {
	LuaValue val = args.at(0);
	if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(val)) {
		auto co = std::get<std::shared_ptr<LuaCoroutine>>(val);
		switch (co->status) {
			case LuaCoroutine::Status::SUSPENDED: return {"suspended"};
			case LuaCoroutine::Status::RUNNING: return {"running"};
			case LuaCoroutine::Status::DEAD: return {"dead"};
		}
	}
	return {std::monostate{}, "invalid thread"};
}

std::vector<LuaValue> coroutine_running(std::vector<LuaValue> args) {
	// Note: With parallelism, checking current_coroutine is still valid 
	// because it is thread_local.
	if (current_coroutine) {
		// We can't return the shared_ptr easily from the raw pointer here
		// without an enabling_shared_from_this refactor, returning dummy.
		return {std::monostate{}, false}; 
	}
	return {std::monostate{}, true};
}

std::vector<LuaValue> coroutine_close(std::vector<LuaValue> args) {
	LuaValue val = args.at(0);
	if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(val)) {
		// Cannot force-close a thread blocked on CV in this implementation
		return {true};
	}
	return {false, "invalid thread"};
}

std::vector<LuaValue> coroutine_isyieldable(std::vector<LuaValue> args) {
	return {current_coroutine != nullptr};
}

std::vector<LuaValue> coroutine_wrap(std::vector<LuaValue> args) {
	// wrap returns a function that calls resume
	auto create_res = coroutine_create(args);
	if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(create_res[0])) {
		auto co = std::get<std::shared_ptr<LuaCoroutine>>(create_res[0]);
		auto wrapper = std::make_shared<LuaFunctionWrapper>([co](std::vector<LuaValue> wrap_args) -> std::vector<LuaValue> {
			auto res = co->resume(wrap_args);
			if (std::holds_alternative<bool>(res[0]) && !std::get<bool>(res[0])) {
				// If resume failed (returned false, err), wrap should error
				if (res.size() > 1) throw std::runtime_error(to_cpp_string(res[1]));
				throw std::runtime_error("coroutine resume failed");
			}
			// Remove the boolean 'true' from the result
			res.erase(res.begin());
			return res;
		});
		return {wrapper};
	}
	throw std::runtime_error("coroutine.wrap failed");
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