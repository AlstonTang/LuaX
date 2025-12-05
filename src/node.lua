-- Node class - AST node representation
-- This module provides the Node class used throughout the translator

local Node = {}
Node.__index = Node

function Node:new(type, value, identifier)
	local instance = setmetatable({}, Node)
	instance.type = type
	instance.value = value
	instance.identifier = identifier
	instance.parent = nil
	instance.ordered_children = {}
	return instance
end

function Node:SetParent(parent)
	self.parent = parent
end

function Node:AddChildren(...)
	for _, child in ipairs({...}) do
		if self.type == "Root" and child.type == "Root" then
			print("ERROR: Node:AddChildren adding Root to Root! Parent: " .. tostring(self) .. " Child: " .. tostring(child))
			os.exit(1)
		end
		table.insert(self.ordered_children, child)
		child:SetParent(self)
	end
end

function Node:get_all_children_of_type(type_name)
	local matching_children = {}
	for _, child in ipairs(self.ordered_children) do
		if child.type == type_name then
			table.insert(matching_children, child)
		end
	end
	return matching_children
end

function Node:find_child_by_type(type_name)
	for _, child in ipairs(self.ordered_children) do
		if child.type == type_name then
			return child
		end
	end
	return nil
end

function Node:GenerateIterator(complete_stack)
	local stack = {}
	local function traverse(node)
		table.insert(stack, node)
		for _, child in ipairs(node.ordered_children) do
			traverse(child)
		end
	end

	if complete_stack then
		traverse(self)
		local i = 0
		return function()
			i = i + 1
			return stack[i]
		end
	else
		local i = 0
		local function get_current_node_children()
			i = i + 1
			return self.ordered_children[i]
		end
		return get_current_node_children
	end
end

return Node