-- Performance test for library function caching
local thing = 1
for i=1, 100000 do
    thing = thing + math.sin(i)
end
print(thing)

-- Also test other library functions
local str = "hello world"
local result = string.sub(str, 1, 5)
print(result)

-- Test multiple library accesses in same scope
local x = math.floor(3.7) + math.ceil(2.3)
print(x)
