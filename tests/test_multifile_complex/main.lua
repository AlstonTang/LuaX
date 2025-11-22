-- main.lua
print("Starting Multi-file Test")

local utils = require("utils")
local data = require("data")
local calc = require("math.calc")

print("Loaded modules")

utils.log("Testing utils.log")

if data.version ~= "1.0" then
    error("Data version mismatch")
end

local result = calc.add(10, 20)
if result ~= 30 then
    error("Calc add failed")
end

local result2 = calc.sub(30, 10)
if result2 ~= 20 then
    error("Calc sub failed")
end

print("Multi-file Test Passed!")
