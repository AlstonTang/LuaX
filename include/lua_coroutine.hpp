#ifndef LUA_COROUTINE_HPP
#define LUA_COROUTINE_HPP

#include "lua_object.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <functional>

class LuaCoroutine : public LuaObject {
public:
    enum class Status {
        SUSPENDED,
        RUNNING,
        DEAD,
        NORMAL // For the main thread or when a coroutine resumes another
    };

    std::thread worker;
    std::mutex mtx;
    std::condition_variable cv_resume; // Signal to resume execution
    std::condition_variable cv_yield;  // Signal that execution has yielded/finished

    std::vector<LuaValue> args;    // Arguments passed to resume/yield
    std::vector<LuaValue> results; // Results returned from yield/return
    
    Status status;
    bool started;
    
    // The function to be executed
    std::shared_ptr<LuaFunctionWrapper> func;

    LuaCoroutine(std::shared_ptr<LuaFunctionWrapper> f);
    ~LuaCoroutine();

    // Called by the creator/resumer
    std::vector<LuaValue> resume(std::vector<LuaValue> resume_args);

    // Called by the coroutine itself
    static std::vector<LuaValue> yield(std::vector<LuaValue> yield_args);
    
    // The worker thread function
    void run();
};

#endif // LUA_COROUTINE_HPP
