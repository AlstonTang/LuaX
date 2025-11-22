-- test_metamethods.lua

print("--- Testing Metamethods ---")

-- Test __index as a function
local mytable = {}
local mt_index_func = {
    __index = function(t, k)
        print("Accessing non-existent key:", k)
        return "default_value_" .. k
    end
}
setmetatable(mytable, mt_index_func)

print("mytable.a:", mytable.a) -- Should trigger __index
print("mytable.b:", mytable.b) -- Should trigger __index

-- Test __index as a table
local mytable2 = { existing = "hello" }
local default_values = {
    x = 100,
    y = 200
}
local mt_index_table = {
    __index = default_values
}
setmetatable(mytable2, mt_index_table)

print("mytable2.existing:", mytable2.existing) -- Should return "hello"
print("mytable2.x:", mytable2.x)             -- Should trigger __index (table)
print("mytable2.z:", mytable2.z)             -- Should trigger __index (table)

-- Test __newindex as a function
local mytable3 = {}
local mt_newindex_func = {
    __newindex = function(t, k, v)
        print("Attempting to set non-existent key:", k, "with value:", v)
        rawset(t, "log_" .. k, v) -- Store in a different key
    end
}
setmetatable(mytable3, mt_newindex_func)

mytable3.new_key = "some_value" -- Should trigger __newindex
print("mytable3.new_key (should be nil):", mytable3.new_key)
print("mytable3.log_new_key (should be 'some_value'):", mytable3.log_new_key)

-- Test __newindex as a table
local mytable4 = {}
local storage_table = {}
local mt_newindex_table = {
    __newindex = storage_table
}
setmetatable(mytable4, mt_newindex_table)

mytable4.another_key = "another_value" -- Should trigger __newindex (table)
print("mytable4.another_key (should be nil):", mytable4.another_key)
print("storage_table.another_key (should be 'another_value'):", storage_table.another_key)

print("--- Metamethods Test Complete ---")
