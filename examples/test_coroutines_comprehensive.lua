-- test_coroutines_comprehensive.lua

print("Starting Comprehensive Coroutine Tests")

-- Helper function to assert conditions
local function assert_true(cond, msg)
    if not cond then
        print("FAILED: " .. msg)
        os.exit(1)
    else
        print("PASSED: " .. msg)
    end
end

local function assert_equal(a, b, msg)
    if a ~= b then
        print("FAILED: " .. msg .. " (Expected " .. tostring(b) .. ", got " .. tostring(a) .. ")")
        os.exit(1)
    else
        print("PASSED: " .. msg)
    end
end

-- 1. Basic create and resume
print("\n--- Test 1: Basic Create and Resume ---")
local co = coroutine.create(function()
    print("Coroutine started")
    return "finished"
end)

assert_equal(type(co), "thread", "coroutine.create returns a thread")
assert_equal(coroutine.status(co), "suspended", "New coroutine is suspended")

local success, result = coroutine.resume(co)
assert_true(success, "coroutine.resume success")
assert_equal(result, "finished", "coroutine return value")
assert_equal(coroutine.status(co), "dead", "Coroutine is dead after finishing")

-- 2. Yield and Resume with values
print("\n--- Test 2: Yield and Resume with values ---")
local co2 = coroutine.create(function(a, b)
    print("Coroutine 2 started with", a, b)
    local c = coroutine.yield(a + b)
    print("Coroutine 2 resumed with", c)
    return c * 2
end)

local s, r1 = coroutine.resume(co2, 10, 20)
assert_true(s, "First resume success")
assert_equal(r1, 30, "Yielded value")
assert_equal(coroutine.status(co2), "suspended", "Coroutine suspended after yield")

local s2, r2 = coroutine.resume(co2, 5)
assert_true(s2, "Second resume success")
assert_equal(r2, 10, "Final return value")
assert_equal(coroutine.status(co2), "dead", "Coroutine dead after finish")

-- 3. coroutine.wrap
print("\n--- Test 3: coroutine.wrap ---")
local wrapped = coroutine.wrap(function(val)
    print("Wrapped coroutine running with", val)
    coroutine.yield(val * 2)
    return val * 3
end)

assert_equal(type(wrapped), "function", "coroutine.wrap returns a function")
local w1 = wrapped(5)
assert_equal(w1, 10, "Wrapped yield value")
local w2 = wrapped()
assert_equal(w2, 15, "Wrapped return value")

-- 4. Error handling in coroutines
print("\n--- Test 4: Error handling ---")
local co_err = coroutine.create(function()
    error("Something went wrong")
end)

local s_err, msg = coroutine.resume(co_err)
assert_true(not s_err, "Resume should fail on error")
assert_true(string.find(msg, "Something went wrong") ~= nil, "Error message propagated")
assert_equal(coroutine.status(co_err), "dead", "Coroutine dead after error")

-- 5. Nested Coroutines (if supported)
print("\n--- Test 5: Nested Coroutines ---")
local co_outer = coroutine.create(function()
    print("Outer started")
    local co_inner = coroutine.create(function()
        print("Inner started")
        coroutine.yield("inner_yield")
        return "inner_done"
    end)
    
    local s, r = coroutine.resume(co_inner)
    print("Outer got from inner:", r)
    coroutine.yield(r)
    
    s, r = coroutine.resume(co_inner)
    print("Outer got from inner:", r)
    return r
end)

local s_nest, r_nest = coroutine.resume(co_outer)
assert_equal(r_nest, "inner_yield", "Outer yielded inner's value")
s_nest, r_nest = coroutine.resume(co_outer)
assert_equal(r_nest, "inner_done", "Outer returned inner's result")

print("\nAll Comprehensive Coroutine Tests Passed!")
