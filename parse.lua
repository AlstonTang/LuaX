function tokenize(code)
    local tokens = {}
    local sep = " "

    for str in code:gmatch("%S+") do
        table.insert(tokens, str)
    end

    return tokens
end

local scriptName = arg[1]

if not scriptName then
    print("Invalid.")
    return
end

local script = io.open(scriptName, "r")
local rawContent = ""

if script then
    rawContent = script:read("*all") -- Read the entire content of the file
    script:close() -- Close the file
else
    print("Error: Could not open the file " .. scriptName)
    return
end

local tokens = tokenize(rawContent)

for i, v in ipairs(tokens) do
    print(i, v)
end