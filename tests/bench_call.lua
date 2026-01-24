local function add(a, b)
    return a + b
end

local start = os.clock()
local sum = 0
for i = 1, 10000000 do
    sum = add(sum, 1)
end
local stop = os.clock()

print("Time: " .. (stop - start) .. "s")
print("Sum: " .. sum)
