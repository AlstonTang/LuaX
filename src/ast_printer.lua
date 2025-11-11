local ASTPrinter = {}

function ASTPrinter.print_ast(node, indent_level)
    indent_level = indent_level or 0
    local indent = string.rep("  ", indent_level)
    local output = indent .. "Type: " .. node.type

    if node.value then
        output = output .. ", Value: " .. tostring(node.value)
    end
    if node.identifier then
        output = output .. ", Identifier: " .. node.identifier
    end
    if node.method_name then
        output = output .. ", MethodName: " .. node.method_name
    end

    output = output .. "\n"

    if node.ordered_children then
        for _, child in ipairs(node.ordered_children) do
            output = output .. ASTPrinter.print_ast(child, indent_level + 1)
        end
    end
    return output
end

return ASTPrinter
