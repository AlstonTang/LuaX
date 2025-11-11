local M = {}

M.name = "other_module"
M.version = "1.0"

function M.greet(name)
    print("Hello, ", name, "!")
end

return M