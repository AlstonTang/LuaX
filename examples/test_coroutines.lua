print("Testing Coroutines...")

-- Test 1: Basic create/resume/yield
print("Test 1: Basic flow")
local co = coroutine.create(function(a, b)
    print("Coroutine started with", a, b)
    local y1 = coroutine.yield(a + b)
    print("Coroutine resumed with", y1)
    return y1 * 2
end)

print("Status:", coroutine.status(co))
local success, res1 = coroutine.resume(co, 10, 20)
print("Main resumed 1:", success, res1)
assert(success)
assert(res1 == 30)
print("Status:", coroutine.status(co))

local success2, res2 = coroutine.resume(co, 5)
print("Main resumed 2:", success2, res2)
assert(success2)
assert(res2 == 10)
print("Status:", coroutine.status(co))

-- Test 2: coroutine.wrap
print("\nTest 2: coroutine.wrap")
local wrapper = coroutine.wrap(function(x)
    print("Wrapped coroutine running with", x)
    coroutine.yield(x * x)
    return "done"
end)

local res3 = wrapper(5)
print("Wrapper result 1:", res3)
assert(res3 == 25)

local res4 = wrapper()
print("Wrapper result 2:", res4)
assert(res4 == "done")

-- Test 3: Multiple coroutines
print("\nTest 3: Multiple coroutines")
local co1 = coroutine.create(function()
    for i = 1, 3 do
        print("co1", i)
        coroutine.yield()
    end
end)

local co2 = coroutine.create(function()
    for i = 1, 3 do
        print("co2", i)
        coroutine.yield()
    end
end)

for i = 1, 4 do
    coroutine.resume(co1)
    coroutine.resume(co2)
end

print("All tests passed!")
