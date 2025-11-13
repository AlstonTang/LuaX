-- Test file for utf8 library

print("--- Testing utf8.char ---")
print("utf8.char(97):", utf8.char(97)) -- 'a'
print("utf8.char(65):", utf8.char(65)) -- 'A'
print("utf8.char(97, 98, 99):", utf8.char(97, 98, 99)) -- 'abc'
print("utf8.char(8364):", utf8.char(8364)) -- '€' (Euro sign)
print("utf8.char(127843):", utf8.char(127843)) -- '🍣' (Sushi emoji)

print("\n--- Testing utf8.charpattern ---")
print("utf8.charpattern:", utf8.charpattern)

print("\n--- Testing utf8.codepoint ---")
print("utf8.codepoint('abc'):", utf8.codepoint('abc')) -- 97
print("utf8.codepoint('abc', 2):", utf8.codepoint('abc', 2)) -- 98
print("utf8.codepoint('abc', 1, 3):", utf8.codepoint('abc', 1, 3)) -- 97, 98, 99
print("utf8.codepoint('Hello World', 7):", utf8.codepoint('Hello World', 7)) -- 87 (W)
local cp1, cp2, cp3 = utf8.codepoint('€', 1, -1)
print("utf8.codepoint('€', 1, -1):", cp1, cp2, cp3) -- 8364
local cp_sushi = utf8.codepoint('🍣', 1)
print("utf8.codepoint('🍣', 1):", cp_sushi) -- 127843
local cp_ni, cp_hao = utf8.codepoint('你好', 1, -1)
print("utf8.codepoint('你好', 1, -1):", cp_ni, cp_hao) -- 20320, 22909

print("\n--- Testing utf8.len ---")
print("utf8.len('hello'):", utf8.len('hello')) -- 5
print("utf8.len('你好'):", utf8.len('你好')) -- 2
print("utf8.len('€'):", utf8.len('€')) -- 1
print("utf8.len('🍣'):", utf8.len('🍣')) -- 1
print("utf8.len('Hello你好World€🍣'):", utf8.len('Hello你好World€🍣')) -- 14

print("\n--- Testing utf8.offset ---")
print("utf8.offset('hello', 1):", utf8.offset('hello', 1)) -- 1
print("utf8.offset('hello', 3):", utf8.offset('hello', 3)) -- 3
print("utf8.offset('hello', -1):", utf8.offset('hello', -1)) -- 5
print("utf8.offset('hello', 0):", utf8.offset('hello', 0)) -- 1
print("utf8.offset('你好', 1):", utf8.offset('你好', 1)) -- 1
print("utf8.offset('你好', 2):", utf8.offset('你好', 2)) -- 4 (byte offset for '好')
print("utf8.offset('你好', -1):", utf8.offset('你好', -1)) -- 4
print("utf8.offset('你好', -2):", utf8.offset('你好', -2)) -- 1
print("utf8.offset('Hello你好World€🍣', 1):", utf8.offset('Hello你好World€🍣', 1)) -- 1
print("utf8.offset('Hello你好World€🍣', 6):", utf8.offset('Hello你好World€🍣', 6)) -- 6 (byte offset for '你')
print("utf8.offset('Hello你好World€🍣', 8):", utf8.offset('Hello你好World€🍣', 8)) -- 12 (byte offset for 'W')
print("utf8.offset('Hello你好World€🍣', -1):", utf8.offset('Hello你好World€🍣', -1)) -- 20 (byte offset for '🍣')
print("utf8.offset('Hello你好World€🍣', -2):", utf8.offset('Hello你好World€🍣', -2)) -- 17 (byte offset for '€')