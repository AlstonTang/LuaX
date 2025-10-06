local stack = require "stack"

function tokenize(code)
	local equalCommentCurrentCount = -1

    local tokens = {}
    local sep = " "

    for str in code:gmatch("%S+") do
		::back_to_start::
		if str:find("--[", 1, true) then
			local _, indexAt = str:find("--[", 1, true)
			local _, endAt = str:find("[", indexAt + 1, true)
			if endAt then
				equalCommentCurrentCount = endAt - indexAt - 1
			end
		end

		if equalCommentCurrentCount >= 0 and str:find("]", 1, true) then
			local indexAt = str:find("]", 1, true)
			local endAt = str:find("]", indexAt + 1, true)

			if (endAt - indexAt - 1) == equalCommentCurrentCount then
				equalCommentCurrentCount = -1
				str = str:sub(endAt + 1)
				goto back_to_start
			end
		end

		if str:find("--", 1, true) then goto continue end
		
		if equalCommentCurrentCount < 0 then
        	table.insert(tokens, str)
		end

		::continue::
    end

    return tokens
end

local scriptName = arg[1]

if not scriptName then
    print("Invalid.")
    return
end

local scriptFile = io.open(scriptName, "r")
local rawContent = ""

if scriptFile then
    rawContent = scriptFile:read("*all") -- Read the entire content of the file
    scriptFile:close() -- Close the file
else
    print("Error: Could not open the file " .. scriptName)
    return
end

local tokens = tokenize(rawContent)

for i, v in ipairs(tokens) do
    print(i, v)
end