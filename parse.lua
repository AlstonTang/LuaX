function tokenize(code)
    local tokens = {}
    local sep = " "

    for str in code:gmatch("%S+") do
        table.insert(tokens, str)
    end

    return tokens
end

-- Had to use gemini lol regex is difficult
function removeComments(code)
    -- 1. Remove Long Bracket Comments (e.g., --[[ ... ]], --[=[ ... ]=])
    -- The pattern captures the optional balancing equals signs (Group 1) and uses %1 to ensure the closing brackets match the opening.
    -- We replace the entire matched comment block with an empty string "".
    code = code:gsub("---%[(=*)%[.-%]%1%]", "")

    -- 2. Remove Single Line Comments (e.g., -- comment until end of line)
    -- This matches "--" followed by any characters that are not a newline, replacing it with "".
    code = code:gsub("---[^\n]*", "")

    return code
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

local commentRemoved = removeComments(rawContent)

local tokens = tokenize(commentRemoved)

for i, v in ipairs(tokens) do
    print(i, v)
end