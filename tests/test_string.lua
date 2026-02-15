-- test_string.lua
local s = "hello world"
assert(#s == 11, "String length failed")
assert(string.len(s) == 11, "string.len failed")
assert(string.byte(s) == 104, "string.byte failed") 
local s2 = s .. "!"
assert(s2 == "hello world!", "Concatenation failed")
print("PASS: String tests")
