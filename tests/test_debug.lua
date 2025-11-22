local status, result = pcall(function()
    debug.getinfo(1)
end)
print("pcall(debug.getinfo):", status, result)