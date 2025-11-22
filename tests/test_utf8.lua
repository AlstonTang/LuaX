local s = "hello, world"
for p, c in utf8.codes(s) do
  print(p, c)
end