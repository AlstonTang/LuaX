-- test_override.lua
local results = {}

-- 1. Redefine math.sin
local original_sin = math.sin
math.sin = function(x) return 42 end

-- If inlining is disabled, this should return 42.
if math.sin(0) == 42 then
    table.insert(results, "PASS: math.sin override respected")
else
    table.insert(results, "FAIL: math.sin override IGNORED (inlining unsafe)")
end

math.sin = original_sin

-- 2. Redefine os.clock
os.clock = function() return 12345 end
if os.clock() == 12345 then
    table.insert(results, "PASS: os.clock override respected")
else
    table.insert(results, "FAIL: os.clock override IGNORED")
end

for _, r in ipairs(results) do print(r) end
