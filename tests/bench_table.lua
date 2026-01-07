
local t = { a = 1, b = 2, c = 3 }
local count = tonumber(arg[1]) or 1000000

local t0 = os.clock()
local sum = 0
for i=1,count do
    sum = sum + t.a + t.b + t.c
end
print(sum, os.clock() - t0)
