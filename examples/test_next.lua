local t = {a = 10, b = 20, c = 30}
print("t.a:", t["a"])
print("t.b:", t["b"])
print("t.c:", t["c"])

local k, v = next(t)
while k do
    print(k, t[k]) -- Use native indexing here
    k, v = next(t, k)
end

local empty_t = {}
local k_empty, v_empty = next(empty_t)
print("next(empty_t):", k_empty, v_empty)