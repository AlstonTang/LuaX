-- tests/test_parallel.lua

print("Starting Parallel Coroutine Stress Test")

local function assert_equal(a, b, msg)
    if a ~= b then
        print("FAILED: " .. msg .. " (Expected " .. tostring(b) .. ", got " .. tostring(a) .. ")")
        os.exit(1)
    end
end

local function worker_func(id, loops)
    local sum = 0
    for i = 1, loops do
        sum = sum + i
    end
    return sum, id
end

local threads = {}
local count = 50
local loops = 10000

print("Creating " .. count .. " parallel threads...")

for i = 1, count do
    -- Create parallel coroutine
    local co = coroutine.create_parallel(worker_func)
    -- Resume starts it (async)
    coroutine.resume(co, i, loops)
    table.insert(threads, co)
end

print("Waiting for threads...")

for i, co in ipairs(threads) do
    local success, sum, id = coroutine.await(co)
    assert_equal(success, true, "Thread " .. i .. " success")
    
    local expected_sum = (loops * (loops + 1)) / 2
    
    if sum ~= expected_sum then
        print("FAILED: Thread " .. i .. " returned wrong sum: " .. tostring(sum))
        os.exit(1)
    end
    
    if id ~= i then
        print("FAILED: Thread " .. i .. " returned wrong ID: " .. tostring(id))
        os.exit(1)
    end
end

print("PASSED: Parallel stress test completed successfully.")
