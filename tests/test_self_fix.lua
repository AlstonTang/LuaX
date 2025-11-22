local Class = {}
Class.__index = Class

function Class:new()
    local instance = setmetatable({}, Class)
    instance.value = 42
    return instance
end

function Class:method()
    print("Value is: " .. self.value)
end

local obj = Class:new()
obj:method()
