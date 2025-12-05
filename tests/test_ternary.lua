-- Test ternary expressions
local x = 5
local result = x > 3 and "big" or "small"
print(result) -- Expected: big

local y = 2
local result2 = y > 3 and "big" or "small"
print(result2) -- Expected: small

-- Nested ternary
local z = 10
local result3 = z > 5 and (z > 8 and "huge" or "large") or "small"
print(result3) -- Expected: huge

-- Ternary in function call
print(x == 5 and "x is 5" or "x is not 5") -- Expected: x is 5

-- Ternary via function call
function test()
    return false
end

print(test() and "Should not output" or "Correct")
