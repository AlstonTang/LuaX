#include "lua_coroutine.hpp"
#include "lua_object.hpp"
#include <stdexcept>
#include <iostream>

// Global pointer to the currently running coroutine
// nullptr means the main thread is running
static LuaCoroutine* current_coroutine = nullptr;

// --- LuaCoroutine Implementation ---

LuaCoroutine::LuaCoroutine(std::shared_ptr<LuaFunctionWrapper> f) 
    : func(f), status(Status::SUSPENDED), started(false) {
    // The thread is spawned in the constructor but waits for the first resume
    worker = std::thread(&LuaCoroutine::run, this);
}

LuaCoroutine::~LuaCoroutine() {
    {
        std::unique_lock<std::mutex> lock(mtx);
        if (status != Status::DEAD) {
            // If destroyed while running or suspended, we might need to detach or join
            // For simplicity, if it's not dead, we detach.
            // In a real implementation, we might want to signal it to exit.
            worker.detach();
        } else {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
}

void LuaCoroutine::run() {
    // Wait for the first resume
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv_resume.wait(lock, [this] { return started; });
    }

    // Execute the function
    try {
        auto args_ptr = std::make_shared<LuaObject>();
        for (size_t i = 0; i < args.size(); ++i) {
            args_ptr->set(std::to_string(i + 1), args[i]);
        }
        
        current_coroutine = this;
        status = Status::RUNNING;
        
        std::vector<LuaValue> res = func->func(args_ptr);
        
        // Coroutine finished naturally
        {
            std::lock_guard<std::mutex> lock(mtx);
            results = res;
            status = Status::DEAD;
        }
    } catch (const std::exception& e) {
        // Coroutine finished with error
        {
            std::lock_guard<std::mutex> lock(mtx);
            results = {false, std::string(e.what())}; 
            status = Status::DEAD;
            error_occurred = true;
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            results = {false, "unknown error"};
            status = Status::DEAD;
        }
    }

    current_coroutine = nullptr; // Back to main thread (conceptually)
    cv_yield.notify_one();
}

std::vector<LuaValue> LuaCoroutine::resume(std::vector<LuaValue> resume_args) {
    std::unique_lock<std::mutex> lock(mtx);

    if (status == Status::DEAD) {
        return {false, "cannot resume dead coroutine"};
    }
    if (status == Status::RUNNING) {
        return {false, "cannot resume running coroutine"};
    }

    // Pass arguments to the coroutine
    args = resume_args;
    
    if (!started) {
        started = true;
    }
    
    status = Status::RUNNING;
    
    // Signal worker to run
    cv_resume.notify_one();
    
    // Wait for worker to yield or finish
    cv_yield.wait(lock, [this] { return status == Status::SUSPENDED || status == Status::DEAD; });

    // Return results from yield/return
    std::vector<LuaValue> res;
    if (error_occurred) {
        res = results;
    } else {
        res.push_back(true); // Success
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

    // Set results to be returned to resume()
    self->results = yield_args;
    self->status = Status::SUSPENDED;

    // Signal resumer
    self->cv_yield.notify_one();

    // Wait for next resume
    self->cv_resume.wait(lock, [self] { return self->status == Status::RUNNING; });

    // Restore current_coroutine to self, as we are now running again
    current_coroutine = self;

    // Return arguments passed to resume()
    return self->args;
}


// --- Global `coroutine` Library Functions ---

std::vector<LuaValue> coroutine_create(std::shared_ptr<LuaObject> args) {
    LuaValue val = args->get("1");
    if (std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(val)) {
        auto func = std::get<std::shared_ptr<LuaFunctionWrapper>>(val);
        auto co = std::make_shared<LuaCoroutine>(func);
        return {co};
    }
    throw std::runtime_error("bad argument #1 to 'create' (function expected)");
}

std::vector<LuaValue> coroutine_resume(std::shared_ptr<LuaObject> args) {
    LuaValue val = args->get("1");
    if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(val)) {
        auto co = std::get<std::shared_ptr<LuaCoroutine>>(val);
        
        std::vector<LuaValue> resume_args;
        for (int i = 2; ; ++i) {
            auto key = std::to_string(i);
            if (!args->properties.count(key)) break;
            resume_args.push_back(args->get(key));
        }
        
        return co->resume(resume_args);
    }
    throw std::runtime_error("bad argument #1 to 'resume' (thread expected)");
}

std::vector<LuaValue> coroutine_yield(std::shared_ptr<LuaObject> args) {
    std::vector<LuaValue> yield_args;
    for (int i = 1; ; ++i) {
        auto key = std::to_string(i);
        if (!args->properties.count(key)) break;
        yield_args.push_back(args->get(key));
    }
    return LuaCoroutine::yield(yield_args);
}

std::vector<LuaValue> coroutine_status(std::shared_ptr<LuaObject> args) {
    LuaValue val = args->get("1");
    if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(val)) {
        auto co = std::get<std::shared_ptr<LuaCoroutine>>(val);
        switch (co->status) {
            case LuaCoroutine::Status::SUSPENDED: return {std::string(co->started ? "suspended" : "suspended")}; // Lua distinguishes "suspended" vs "normal" but our model is simpler
            case LuaCoroutine::Status::RUNNING: return {"running"};
            case LuaCoroutine::Status::DEAD: return {"dead"};
            case LuaCoroutine::Status::NORMAL: return {"normal"};
        }
    }
    return {std::monostate{}, "invalid thread"};
}

std::vector<LuaValue> coroutine_running(std::shared_ptr<LuaObject> args) {
    if (current_coroutine) {
        // We need a shared_ptr to the current coroutine.
        // Since LuaCoroutine inherits from enable_shared_from_this, we can get it.
        // However, current_coroutine is a raw pointer.
        // We should probably pass shared_ptr around or use shared_from_this if we have it.
        // But we don't have easy access to the shared_ptr from the raw pointer unless we store it.
        // For now, let's return nil and true (is_main) if null, or the coroutine object.
        // To do this safely, we might need to store the shared_ptr in the thread local storage or similar.
        // Or just accept that we can't easily return the shared_ptr from the raw pointer here without more infrastructure.
        // Let's return nil for now if we can't get the shared_ptr.
        return {std::monostate{}, false}; 
    }
    return {std::monostate{}, true}; // Main thread
}

std::vector<LuaValue> coroutine_wrap(std::shared_ptr<LuaObject> args) {
    // wrap returns a function that calls resume
    auto create_res = coroutine_create(args);
    if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(create_res[0])) {
        auto co = std::get<std::shared_ptr<LuaCoroutine>>(create_res[0]);
        auto wrapper = std::make_shared<LuaFunctionWrapper>([co](std::shared_ptr<LuaObject> wrap_args) -> std::vector<LuaValue> {
            std::vector<LuaValue> resume_args;
            for (int i = 1; ; ++i) {
                auto key = std::to_string(i);
                if (!wrap_args->properties.count(key)) break;
                resume_args.push_back(wrap_args->get(key));
            }
            auto res = co->resume(resume_args);
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

std::vector<LuaValue> coroutine_isyieldable(std::shared_ptr<LuaObject> args) {
    return {current_coroutine != nullptr};
}

std::vector<LuaValue> coroutine_close(std::shared_ptr<LuaObject> args) {
     LuaValue val = args->get("1");
    if (std::holds_alternative<std::shared_ptr<LuaCoroutine>>(val)) {
        // In our simple model, we can't easily force-close a thread blocked on CV.
        // But if it's suspended, we can maybe signal it to exit?
        // For now, stub it.
        return {true};
    }
    return {false, "invalid thread"};
}

std::shared_ptr<LuaObject> create_coroutine_library() {
    auto coroutine_lib = std::make_shared<LuaObject>();

    coroutine_lib->set("create", std::make_shared<LuaFunctionWrapper>(coroutine_create));
    coroutine_lib->set("resume", std::make_shared<LuaFunctionWrapper>(coroutine_resume));
    coroutine_lib->set("yield", std::make_shared<LuaFunctionWrapper>(coroutine_yield));
    coroutine_lib->set("status", std::make_shared<LuaFunctionWrapper>(coroutine_status));
    coroutine_lib->set("running", std::make_shared<LuaFunctionWrapper>(coroutine_running));
    coroutine_lib->set("wrap", std::make_shared<LuaFunctionWrapper>(coroutine_wrap));
    coroutine_lib->set("isyieldable", std::make_shared<LuaFunctionWrapper>(coroutine_isyieldable));
    coroutine_lib->set("close", std::make_shared<LuaFunctionWrapper>(coroutine_close));

    return coroutine_lib;
}