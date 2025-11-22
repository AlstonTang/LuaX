local status, err = pcall(function()
    error("This is an error from pcall test")
end)

if not status then
    print("Caught error:", err)
else
    print("No error caught, status:", status)
end

print("Script finished.")