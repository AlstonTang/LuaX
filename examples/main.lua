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

for i=1, 100000 do
    thing = thing + math.sin(i)
end

print(thing)