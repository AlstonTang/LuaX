local M = {}

M.name = "other_module"
M.version = "1.0"

function M.greet(name)
    return "Hello, " .. name .. " from " .. M.name .. " v" .. M.version .. "!"
end

return M