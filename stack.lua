local LinkedList = require("linkedlist")

local Stack = {}
Stack.__index = Stack

function Stack.new()
    local self = setmetatable({}, Stack)

    self.contents = nil

    return self
end

function Stack:Peek()
    return self.contents and self.contents.val
end

function Stack:Pop()
    if self.contents then
        local out = self:Peek()
        self.contents = self.contents.after
        return out
    end
end

function Stack:Push(val)
    self.contents = LinkedList.new(val, self.contents)
end

return Stack