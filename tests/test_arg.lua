-- examples/test_arg.lua
print("Script name:", arg[0])
print("Type of arg[0]:", type(arg[0]))
print("Number of arguments:", #arg)
print("Type of #arg:", type(#arg))
for i = 1, #arg do
    print("arg[" .. i .. "]:", arg[i])
    print("Type of arg[" .. i .. "]:", type(arg[i]))
end