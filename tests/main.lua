-- main.lua

-- 1. Simple snippets: variables, arithmetic, print
local a = 10
local b = 20
local c = a + b
print("Sum:", c)

local name = "World"
print("Hello,", name)

-- 2. If, for, while loops
if a > b then
    print("a is greater than b")
elseif a < b then
    print("a is less than b")
else
    print("a is equal to b")
end

for i = 1, 3 do
    print("For loop iteration:", i)
end

local count = 0
while count < 2 do
    print("While loop iteration:", count)
    count = count + 1
end

-- 3. Lua string matching
local text = "hello world"
local pattern = "world"
if string.match(text, pattern) then
    print("Pattern found!")
end

local pos = string.find(text, "world")
print("Pattern 'world' found at position:", pos)

local new_text = string.gsub(text, "world", "lua")
print("String gsub:", new_text)

-- 4. Multi-file support with require (already present)
local other = require("other_module")
print("Module name:", other.name)
print("Module version:", other.version)

local greeting = other.greet("Gemini")
print(greeting)

-- 5. Complex metatable usage: __index and __newindex
-- (already present)

-- Metatable for default values
local defaults = {
    x = 0,
    y = 0,
    color = "blue"
}

local mt = {}
function mt.__index(table, key)
    print("Accessing missing key:", key)
    return defaults[key]
end

function mt.__newindex(table, key, value)
    print("Attempting to set new key:", key, "with value:", value)
    if key == "z" then
        rawset(table, key, value) -- Allow setting 'z' directly
    else
        print("Cannot set key '" .. key .. "'. Use rawset if intended.")
    end
end

local my_object = {}
setmetatable(my_object, mt)

print("my_object.x:", my_object.x)
print("my_object.color:", my_object.color)

my_object.a = 100
my_object.z = 50
print("my_object.z:", my_object.z)

-- Test setting an existing key with __newindex
my_object.x = 99
print("my_object.x (after attempted set):", my_object.x)

print("Begin loop")

thing = 1
print("os.clock right before starting:", os.clock())

for i=1, 100000 do
    thing = thing + math.sin(i)
end

print(thing)

-- String library tests
local test_string = "hello"
print("Length of 'hello':", string.len(test_string))
print("Reverse of 'hello':", string.reverse(test_string))

-- Table library tests
local my_table = { "a", "b", "c" }
print("Original table:")
for i = 1, 3 do
    print(i, rawget(my_table, i))
end

-- OS library tests
print("os.clock:", os.clock())
print("os.time:", os.time())

table.insert(my_table, "d")
print("After insert:")
for i = 1, 4 do
    print(i, rawget(my_table, i))
end

table.remove(my_table, 2)
print("After remove:")
for i = 1, 3 do
    print(i, rawget(my_table, i))
end

-- IO library tests
-- io.write("Enter your name: ")
-- local user_name = io.read("*l")
-- io.write("Hello, ", user_name, "!\n")

-- io.write("Enter a number: ")
-- local num = io.read("*n")
-- io.write("You entered: ", num, "\n")

-- Package library tests
print("package.path:", package.path)
print("package.cpath:", package.cpath)

-- Global constants tests
print("_VERSION:", _VERSION)

-- Global functions tests
print("tonumber('123'):", tonumber("123"))
print("tonumber('hello'):", tonumber("hello"))
print("tonumber(123):", tonumber(123))

print("tostring(123):", tostring(123))
print("tostring('hello'):", tostring("hello"))
print("tostring(true):", tostring(true))

local n = nil
print("type(n):", type(n))
print("type(true):", type(true))
print("type(123):", type(123))
print("type('hello'):", type('hello'))
print("type({}):", type({}))
print("type(function() end):", type(function() end))

-- getmetatable tests
local my_table_with_mt = {}
local mt_for_get = { __index = function() return "metatable_value" end }
setmetatable(my_table_with_mt, mt_for_get)
print("getmetatable(my_table_with_mt):", getmetatable(my_table_with_mt))

-- error tests
pcall(function() error("This is an error message") end)

-- Lua 5.4 floor division and bitwise operator tests
print("Floor division: 7 // 3 =", 7 // 3)  -- Expected: 2
print("Floor division: -7 // 3 =", -7 // 3)  -- Expected: -3
print("Bitwise AND: 7 & 3 =", 7 & 3)  -- Expected: 3
print("Bitwise OR: 7 | 8 =", 7 | 8)  -- Expected: 15
print("Bitwise XOR: 7 ~ 3 =", 7 ~ 3)  -- Expected: 4
print("Unary NOT: ~0 =", ~0)  -- Expected: -1
print("Unary NOT: ~(-1) =", ~(-1))  -- Expected: 0
print("Left shift: 7 << 2 =", 7 << 2)  -- Expected: 28
print("Right shift: 28 >> 2 =", 28 >> 2)  -- Expected: 7