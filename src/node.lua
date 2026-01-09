-- Node class - AST node representation
-- This module provides the Node class used throughout the translator

local Node = {}
Node.__index = Node

-- Mapping for Node:
-- [1] = type
-- [2] = value
-- [3] = identifier
-- [4] = parent
-- [5] = ordered_children

function Node:new(type, value, identifier)
	local instance = {
		type,       -- [1]
		value,      -- [2]
		identifier, -- [3]
		nil,        -- [4] parent
		nil         -- [5] ordered_children (Lazy)
	}
	setmetatable(instance, Node)
	return instance
end

function Node:AddChild(child)
	if not child then return end
	local children = self[5]
	if not children then
		children = {}
		self[5] = children
	end
	table.insert(children, child)
	child[4] = self -- child[4] is child.parent
end

function Node:AddChildren(c1, c2, c3)
	if c1 then self:AddChild(c1) end
	if c2 then self:AddChild(c2) end
	if c3 then self:AddChild(c3) end
end

function Node:get_all_children_of_type(type_name)
	local matching_children = {}
	local children = self[5]
	if not children then return matching_children end
	for i = 1, #children do
		local child = children[i]
		if child[1] == type_name then
			table.insert(matching_children, child)
		end
	end
	return matching_children
end

function Node:find_child_by_type(type_name)
	local children = self[5]
	if not children then return nil end
	for i = 1, #children do
		local child = children[i]
		if child[1] == type_name then
			return child
		end
	end
	return nil
end

function Node:GenerateIterator(complete_stack)
	local stack = {}
	local function traverse(node)
		table.insert(stack, node)
		local children = node[5]
		if children then
			for i = 1, #children do
				traverse(children[i])
			end
		end
	end

	if complete_stack then
		traverse(self)
		local idx = 0
		return function()
			idx = idx + 1
			return stack[idx]
		end
	else
		local idx = 0
		return function()
			idx = idx + 1
			local children = self[5]
			if not children then return nil end
			return children[idx]
		end
	end
end

return Node