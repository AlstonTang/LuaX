local translator = require("translator")

local code = "local x = myFunc(\"hello\")"
local root_node = translator.translate(code)

local function print_node(node, indent_level)
    indent_level = indent_level or 0
    local indent = string.rep("  ", indent_level)
    local output = indent .. "Node type: " .. node.type
    if node.value then
        output = output .. ", Value: " .. tostring(node.value)
    end
    if node.identifier then
        output = output .. ", Identifier: " .. node.identifier
    end
    print(output)

    for _, child in ipairs(node.ordered_children) do
        print_node(child, indent_level + 1)
    end
end

print("\n--- AST Structure ---")
print_node(root_node)

print("\n--- Iterating through all nodes ---")
local iterator = root_node:GenerateIterator(true)

while true do
    local node = iterator()
    if not node then
        break
    end
    local output = "Node type: " .. node.type
    if node.value then
        output = output .. ", Value: " .. tostring(node.value)
    end
    if node.identifier then
        output = output .. ", Identifier: " .. node.identifier
    end
    if node.parent then
        output = output .. ", Parent type: " .. node.parent.type
    end
    print(output)
end