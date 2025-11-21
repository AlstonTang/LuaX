print("Testing IO functions...")

-- Test io.popen
print("Testing io.popen...")
local f = io.popen("echo 'Hello from popen'", "r")
if f then
    local content = f:read("*a")
    print("popen content: " .. content)
    f:close()
    assert(content:match("Hello from popen"))
else
    print("io.popen failed")
end

-- Test io.tmpfile
print("Testing io.tmpfile...")
local tmp = io.tmpfile()
if tmp then
    tmp:write("Temporary data")
    tmp:seek("set", 0)
    local content = tmp:read("*a")
    print("tmpfile content: " .. content)
    tmp:close()
    assert(content == "Temporary data")
else
    print("io.tmpfile failed")
end

-- Test io.lines (global)
print("Testing io.lines...")
local f_lines = io.open("test_lines.txt", "w")
f_lines:write("Line 1\nLine 2\nLine 3")
f_lines:close()

local count = 0
for line in io.lines("test_lines.txt") do
    count = count + 1
    print("Line " .. count .. ": " .. line)
end
assert(count == 3)
os.remove("test_lines.txt")

-- Test io.flush
print("Testing io.flush...")
io.write("Flushing stdout... ")
io.flush()
print("Done.")

-- Test file:setvbuf
print("Testing file:setvbuf...")
local f_buf = io.tmpfile()
f_buf:setvbuf("no")
f_buf:write("Unbuffered")
f_buf:close()

print("All tests passed!")
