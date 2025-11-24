local s = "-- main.lua"
print("s:sub(2, 2) = '" .. s:sub(2, 2) .. "'")
if s:sub(2, 2) == "-" then
    print("OK")
else
    print("FAIL")
end
