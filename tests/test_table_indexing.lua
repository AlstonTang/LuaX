-- Test native Lua table indexing

local my_table = {}

-- Integer indexing
my_table[1] = "hello"
my_table[2] = "world"
my_table[3.0] = "float_key" -- Lua treats 3.0 as integer 3 for table indexing

print("Integer indexed values:")
print(my_table[1]) -- Expected: hello
print(my_table[2]) -- Expected: world
print(my_table[3]) -- Expected: float_key
print(my_table[4]) -- Expected: nil

-- String indexing
my_table["name"] = "LuaX"
my_table["version"] = 1.0

print("\nString indexed values:")
print(my_table["name"])    -- Expected: LuaX
print(my_table["version"]) -- Expected: 1

-- Mixed indexing
my_table["mixed_key"] = 123
my_table[4] = "another_integer"

print("\nMixed indexed values:")
print(my_table["mixed_key"]) -- Expected: 123
print(my_table[4])           -- Expected: another_integer

-- Overwriting
my_table[1] = "new_hello"
print("\nOverwritten value:")
print(my_table[1]) -- Expected: new_hello

-- Table as key (should convert to string representation)
local key_table = {a=1}
my_table[key_table] = "table_as_key"
print("\nTable as key:")
print(my_table[key_table]) -- Expected: table_as_key

-- Nil key (should be an error in Lua, but our system might treat it as string "nil")
-- Lua does not allow nil as a key. This should ideally throw an error.
-- For now, let's see how it behaves.
-- my_table[nil] = "nil_value" -- This would cause a runtime error in Lua
-- print(my_table[nil])

-- Test iteration with next (should iterate over both integer and string keys)
print("\nIterating through table with next:")
local k, v = next(my_table)
while k do
    print(k, v)
    k, v = next(my_table, k)
end

-- Test rawget and rawset
local raw_table = {}
rawset(raw_table, 1, "raw_one")
rawset(raw_table, "two", "raw_two")
rawset(raw_table, 3.0, "raw_three") -- Should be treated as integer 3

print("\nRawget values:")
print(rawget(raw_table, 1))   -- Expected: raw_one
print(rawget(raw_table, "two")) -- Expected: raw_two
print(rawget(raw_table, 3))   -- Expected: raw_three
print(rawget(raw_table, 4))   -- Expected: nil
