count = tonumber(arg[1])
func_name = arg[2]

local t0 = 0
local func = math[func_name]

local accumulate = 0

for i=1,count do
	accumulate = accumulate + func(i)
end

print(accumulate)