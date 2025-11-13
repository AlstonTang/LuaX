function get_two_values()
    return 10, "hello"
end

function get_three_values()
    return 1, 2, 3
end

local a, b = get_two_values()
print("a:", a, "b:", b)

local x, y, z = get_three_values()
print("x:", x, "y:", y, "z:", z)

local p, q = get_three_values() -- p should be 1, q should be 2, 3 should be discarded
print("p:", p, "q:", q)

local r, s, t = get_two_values() -- r should be 10, s should be "hello", t should be nil
print("r:", r, "s:", s, "t:", t)

local single_val = get_two_values() -- single_val should be 10
print("single_val:", single_val)

-- Test global assignment
g1, g2 = get_two_values()
print("g1:", g1, "g2:", g2)
