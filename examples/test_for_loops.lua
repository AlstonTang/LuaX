-- Test numeric for loop
local sum = 0
for i = 1, 5 do
    sum = sum + i
end
print("Sum (1 to 5):", sum) -- Expected: 15

local countdown = ""
for i = 3, 1, -1 do
    countdown = countdown .. i .. " "
end
print("Countdown (3 to 1):", countdown) -- Expected: 3 2 1 

-- Test generic for loop with pairs
local t = {a = 10, b = 20, c = 30}
local keys = ""
local values = ""
for k, v in pairs(t) do
    keys = keys .. k .. " "
    values = values .. v .. " "
end
print("Pairs keys:", keys)   -- Expected: a b c (order might vary)
print("Pairs values:", values) -- Expected: 10 20 30 (order might vary)

-- Test generic for loop with ipairs
local arr = {100, 200, 300, "hello", 500}
local idx_sum = 0
local arr_values = ""
for i, v in ipairs(arr) do
    idx_sum = idx_sum + i
    arr_values = arr_values .. v .. " "
end
print("Ipairs index sum:", idx_sum) -- Expected: 6 (1+2+3)
print("Ipairs values:", arr_values) -- Expected: 100 200 300 hello 
