local Node = {}
Node.__index = Node

function Node:new(type, value, identifier)
	local instance = { type, value, identifier, nil, nil }
	-- Add a dedicated metadata table at index 6 to stop the random [6]/[7] assignments
	instance[6] = {} 
	return setmetatable(instance, Node)
end

-- FIX: Use varargs (...) so it accepts infinite children
function Node:AddChildren(...)
	local children = self[5]
	local args = {...}
	if not children then
		children = {}
		self[5] = children
	end
	for i = 1, #args do
		local child = args[i]
		if child then table.insert(children, child) end
	end
end

function Node:AddChild(child)
	self:AddChildren(child)
end

-- HELPER METHODS: Use these instead of raw index brackets in the future
function Node:type() return self[1] end
function Node:value() return self[2] end
function Node:id() return self[3] end
function Node:children() return self[5] or {} end
function Node:child(index) return self:children()[index] end
function Node:meta() return self[6] end

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