-- bench_math.lua
local N = 10000000
local start = os.clock()
local sum = 0
for i = 1, N do
    sum = sum + math.sin(i)
end
local elapsed = os.clock() - start
print(string.format("Math Benchmark (N=%d): %.4f seconds", N, elapsed))
print("Sum (check):", sum)
