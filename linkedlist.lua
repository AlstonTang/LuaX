local function reverse(tab)
    local i, j = 1, #tab
    while i < j do
        tab[i], tab[j] = tab[j], tab[i]
        i = i + 1
        j = j - 1
    end
    return tab
end

local linkedlist = {}
linkedlist.__index = linkedlist

function linkedlist.new(val, after)
    local self = setmetatable({}, linkedlist)
    
    self.val = val
    self.after = after

    return self
end 

return linkedlist