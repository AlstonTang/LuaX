-- Tokenizer module for LuaX Parser
-- Contains all tokenization logic extracted from translator.lua
-- Optimized for native compilation using byte processing

local byte = string.byte
local sub = string.sub

-- Pre-calculate byte constants for performance
local BYTE_0 = 48 -- byte('0')
local BYTE_9 = 57 -- byte('9')
local BYTE_a = 97 -- byte('a')
local BYTE_z = 122 -- byte('z')
local BYTE_A = 65 -- byte('A')
local BYTE_Z = 90 -- byte('Z')
local BYTE_UNDERSCORE = 95 -- byte('_')
local BYTE_SPACE = 32 -- byte(' ')
local BYTE_TAB = 9 -- byte('\t')
local BYTE_LF = 10 -- byte('\n')
local BYTE_CR = 13 -- byte('\r')

local BYTE_DOT = 46 -- byte('.')
local BYTE_SINGLE_QUOTE = 39 -- byte("'")
local BYTE_DOUBLE_QUOTE = 34 -- byte('"')
local BYTE_BACKSLASH = 92 -- byte('\\')
local BYTE_LBRACKET = 91 -- byte('[')
local BYTE_RBRACKET = 93 -- byte(']')
local BYTE_EQUALS = 61 -- byte('=')
local BYTE_MINUS = 45 -- byte('-')
local BYTE_PLUS = 43 -- byte('+')
local BYTE_STAR = 42 -- byte('*')
local BYTE_SLASH = 47 -- byte('/')
local BYTE_PERCENT = 37 -- byte('%')
local BYTE_HASH = 35 -- byte('#')
local BYTE_AMPERSAND = 38 -- byte('&')
local BYTE_PIPE = 124 -- byte('|')
local BYTE_GT = 62 -- byte('>')
local BYTE_LT = 60 -- byte('<')
local BYTE_TILDE = 126 -- byte('~')
local BYTE_LPAREN = 40 -- byte('(')
local BYTE_RPAREN = 41 -- byte(')')
local BYTE_LBRACE = 123 -- byte('{')
local BYTE_RBRACE = 125 -- byte('}')
local BYTE_COLON = 58 -- byte(':')
local BYTE_COMMA = 44 -- byte(',')

local BYTE_x = 120 -- byte('x')
local BYTE_X = 88 -- byte('X')
local BYTE_f = 102 -- byte('f')
local BYTE_F = 70 -- byte('F')
local BYTE_n = 110 -- byte('n')
local BYTE_t = 116 -- byte('t')
local BYTE_r = 114 -- byte('r')
local BYTE_b = 98 -- byte('b')
local BYTE_a_esc = 97 -- byte('a')
local BYTE_v = 118 -- byte('v')


local function is_digit(c)
	return c and c >= BYTE_0 and c <= BYTE_9
end

local function is_alpha(c)
	return c and ((c >= BYTE_a and c <= BYTE_z) or (c >= BYTE_A and c <= BYTE_Z) or c == BYTE_UNDERSCORE)
end

local function is_whitespace(c)
	return c == BYTE_SPACE or c == BYTE_TAB or c == BYTE_LF or c == BYTE_CR
end

-- Pre-define keyword tokens as array-style to avoid repeated creation and hashing
-- [1] = type, [2] = value
local KW_LOCAL    = { "keyword", "local" }
local KW_FUNCTION = { "keyword", "function" }
local KW_RETURN   = { "keyword", "return" }
local KW_END      = { "keyword", "end" }
local KW_IF       = { "keyword", "if" }
local KW_THEN     = { "keyword", "then" }
local KW_ELSE     = { "keyword", "else" }
local KW_ELSEIF   = { "keyword", "elseif" }
local KW_WHILE    = { "keyword", "while" }
local KW_FOR      = { "keyword", "for" }
local KW_DO       = { "keyword", "do" }
local KW_GOTO     = { "keyword", "goto" }
local KW_BREAK    = { "keyword", "break" }
local KW_REPEAT   = { "keyword", "repeat" }
local KW_UNTIL    = { "keyword", "until" }

local OP_AND      = { "operator", "and" }
local OP_OR       = { "operator", "or" }
local OP_NOT      = { "operator", "not" }

local Tokenizer = {}

function Tokenizer.tokenize(parser)
	local code = parser.code
	local tokens = parser[1]
	local len = #code
	local sub_cache = {}
	local function cached_sub(s, i, j)
		if i == j then return s:sub(i, i) end
		local key = (i * 1000000) + j -- Efficient integer key
		local res = sub_cache[key]
		if res then return res end
		res = s:sub(i, j)
		sub_cache[key] = res
		return res
	end
	
	local current_pos = parser.position
	while current_pos <= len do
		-- Optimized: Use byte access instead of substring
		-- In native mode, this maps to efficient lua_string_byte_at -> long long
		local char_byte = code:byte(current_pos) 
		local token_processed = false 
		
		-- Must handle nil (EOF) gracefully if loop condition doesn't catch it
		if not char_byte then break end

		if is_whitespace(char_byte) then
			current_pos = current_pos + 1
			token_processed = true
		elseif is_digit(char_byte) then
			local start_pos = current_pos
			local is_float = false
			
			-- Check for hexadecimal literal
			local is_hex = false
			if char_byte == BYTE_0 and current_pos + 1 <= len then
				local next_byte = code:byte(current_pos + 1)
				if next_byte == BYTE_x or next_byte == BYTE_X then
					is_hex = true
					current_pos = current_pos + 2 -- Consume '0x'
					local hex_start_pos = current_pos
					while current_pos <= len do
						local hex_byte = code:byte(current_pos)
						if not hex_byte then break end
						if (hex_byte >= BYTE_0 and hex_byte <= BYTE_9) or
						   (hex_byte >= BYTE_a and hex_byte <= BYTE_f) or
						   (hex_byte >= BYTE_A and hex_byte <= BYTE_F) then
							current_pos = current_pos + 1
						else
							break
						end
					end
					local hex_val_str = cached_sub(code, hex_start_pos, current_pos - 1)
					if #hex_val_str == 0 then
						error("Malformed hexadecimal number")
					end
					-- Convert hex string to decimal number
					local decimal_val = tonumber(hex_val_str, 16)
					table.insert(tokens, { "integer", decimal_val })
					token_processed = true
				end
			end

			if not is_hex then
				-- Original decimal number parsing
				while current_pos <= len do
					local current_byte = code:byte(current_pos)
					if is_digit(current_byte) then
						current_pos = current_pos + 1
					elseif current_byte == BYTE_DOT and not is_float then
						is_float = true
						current_pos = current_pos + 1
					else
						break
					end
				end
				local val = cached_sub(code, start_pos, current_pos - 1)
				if is_float then
					table.insert(tokens, { "number", val })
				else
					table.insert(tokens, { "integer", val })
				end
				token_processed = true
			end
			
		elseif is_alpha(char_byte) then
			local start_pos = current_pos
			current_pos = current_pos + 1
			while current_pos <= len do
				local c_byte = code:byte(current_pos)
				if is_alpha(c_byte) or is_digit(c_byte) then
					current_pos = current_pos + 1
				else
					break
				end
			end
			local value = cached_sub(code, start_pos, current_pos - 1)
			if value == "local" then table.insert(tokens, KW_LOCAL)
			elseif value == "function" then table.insert(tokens, KW_FUNCTION)
			elseif value == "return" then table.insert(tokens, KW_RETURN)
			elseif value == "end" then table.insert(tokens, KW_END)
			elseif value == "if" then table.insert(tokens, KW_IF)
			elseif value == "then" then table.insert(tokens, KW_THEN)
			elseif value == "else" then table.insert(tokens, KW_ELSE)
			elseif value == "elseif" then table.insert(tokens, KW_ELSEIF)
			elseif value == "while" then table.insert(tokens, KW_WHILE)
			elseif value == "for" then table.insert(tokens, KW_FOR)
			elseif value == "do" then table.insert(tokens, KW_DO)
			elseif value == "and" then table.insert(tokens, OP_AND)
			elseif value == "or" then table.insert(tokens, OP_OR)
			elseif value == "not" then table.insert(tokens, OP_NOT)
			elseif value == "goto" then table.insert(tokens, KW_GOTO)
			elseif value == "break" then table.insert(tokens, KW_BREAK)
			elseif value == "repeat" then table.insert(tokens, KW_REPEAT)
			elseif value == "until" then table.insert(tokens, KW_UNTIL)
			else table.insert(tokens, { "identifier", value }) end
			token_processed = true


		elseif char_byte == BYTE_MINUS then
			if current_pos + 1 <= len and code:byte(current_pos + 1) == BYTE_MINUS then
				-- Start of comment: consume '--'
				current_pos = current_pos + 2 
				
				local next_byte = code:byte(current_pos)
				
				-- Check for long comment start: --[
				if next_byte == BYTE_LBRACKET then
					current_pos = current_pos + 1 -- consume first '['
					local num_equals = 0
					-- Count equals signs
					while current_pos <= len and code:byte(current_pos) == BYTE_EQUALS do
						num_equals = num_equals + 1
						current_pos = current_pos + 1
					end
					
					local second_bracket_byte = code:byte(current_pos)
					
					if second_bracket_byte == BYTE_LBRACKET then
						-- Confirmed multi-line comment: --[=[...]=]
						current_pos = current_pos + 1 -- consume second '['
						
						-- Determine expected closing sequence
						local expected_end = ']'
						for i = 1, num_equals do
							expected_end = expected_end .. '='
						end
						expected_end = expected_end .. ']'
						local end_len = #expected_end
						
						local comment_end_found = false
						while current_pos <= len do
							-- Check for the closing sequence, ensuring we don't index past the end
							if current_pos + end_len - 1 <= len then
								if code:sub(current_pos, current_pos + end_len - 1) == expected_end then
									current_pos = current_pos + end_len
									comment_end_found = true
									break
								end
							end
							current_pos = current_pos + 1
						end
						
						if not comment_end_found then
							error("Unclosed multi-line comment")
						end
					else
						-- Not a valid long comment start (e.g., --[a or --[=a). Treat as single line.
						while current_pos <= len and code:byte(current_pos) ~= BYTE_LF do
							current_pos = current_pos + 1
						end
					end
				else
					-- Standard single-line comment: --
					while current_pos <= len and code:byte(current_pos) ~= BYTE_LF do
						current_pos = current_pos + 1
					end
				end
			else
				table.insert(tokens, { "operator", "-" })
				current_pos = current_pos + 1
			end
			token_processed = true
		elseif char_byte == BYTE_PLUS or char_byte == BYTE_STAR or char_byte == BYTE_HASH or char_byte == BYTE_PERCENT then
			-- Reconstruct char for value since tokens store strings
			table.insert(tokens, { "operator", string.char(char_byte) })
			current_pos = current_pos + 1
			token_processed = true
		elseif char_byte == BYTE_SLASH then
			-- Check for floor division //
			if current_pos < len and code:byte(current_pos + 1) == BYTE_SLASH then
				table.insert(tokens, { "operator", "//" })
				current_pos = current_pos + 2
			else
				table.insert(tokens, { "operator", "/" })
				current_pos = current_pos + 1
			end
			token_processed = true
		elseif char_byte == BYTE_AMPERSAND then
			table.insert(tokens, { "operator", "&" })
			current_pos = current_pos + 1
			token_processed = true
		elseif char_byte == BYTE_PIPE then
			table.insert(tokens, { "operator", "|" })
			current_pos = current_pos + 1
			token_processed = true
		elseif char_byte == BYTE_EQUALS then
			if current_pos < len and code:byte(current_pos + 1) == BYTE_EQUALS then
				table.insert(tokens, { "operator", "==" })
				current_pos = current_pos + 2
			else
				table.insert(tokens, { "operator", "=" })
				current_pos = current_pos + 1
			end
			token_processed = true
		elseif char_byte == BYTE_GT then
			if current_pos < len and code:byte(current_pos + 1) == BYTE_EQUALS then
				table.insert(tokens, { "operator", ">=" })
				current_pos = current_pos + 2
			elseif current_pos < len and code:byte(current_pos + 1) == BYTE_GT then
				-- Right shift >>
				table.insert(tokens, { "operator", ">>" })
				current_pos = current_pos + 2
			else
				table.insert(tokens, { "operator", ">" })
				current_pos = current_pos + 1
			end
			token_processed = true
		elseif char_byte == BYTE_LT then
			if current_pos < len and code:byte(current_pos + 1) == BYTE_EQUALS then
				table.insert(tokens, { "operator", "<=" })
				current_pos = current_pos + 2
			elseif current_pos < len and code:byte(current_pos + 1) == BYTE_LT then
				-- Left shift <<
				table.insert(tokens, { "operator", "<<" })
				current_pos = current_pos + 2
			else
				table.insert(tokens, { "operator", "<" })
				current_pos = current_pos + 1
			end
			token_processed = true
		elseif char_byte == BYTE_TILDE then
			if current_pos < len and code:byte(current_pos + 1) == BYTE_EQUALS then
				table.insert(tokens, { "operator", "~=" })
				current_pos = current_pos + 2
			else
				-- ~ is bitwise NOT (unary) or bitwise XOR (binary) in Lua 5.3+
				table.insert(tokens, { "operator", "~" })
				current_pos = current_pos + 1
			end
			token_processed = true
		elseif char_byte == BYTE_LPAREN or char_byte == BYTE_RPAREN then
			table.insert(tokens, { "paren", string.char(char_byte) })
			current_pos = current_pos + 1
			token_processed = true
		elseif char_byte == BYTE_DOUBLE_QUOTE or char_byte == BYTE_SINGLE_QUOTE then
			local quote_byte = char_byte
			current_pos = current_pos + 1 -- consume the opening quote
			local buffer = {}
			local start_pos = current_pos
			while current_pos <= len do
				local char_in_byte = code:byte(current_pos)
				if char_in_byte == BYTE_BACKSLASH then
					current_pos = current_pos + 1 -- consume '\'
					local escaped_byte = code:byte(current_pos)
					if escaped_byte == BYTE_n then table.insert(buffer, '\n')
					elseif escaped_byte == BYTE_t then table.insert(buffer, '\t')
					elseif escaped_byte == BYTE_r then table.insert(buffer, '\r')
					elseif escaped_byte == BYTE_b then table.insert(buffer, '\b')
					elseif escaped_byte == BYTE_f then table.insert(buffer, '\f')
					elseif escaped_byte == BYTE_a_esc then table.insert(buffer, '\a')
					elseif escaped_byte == BYTE_v then table.insert(buffer, '\v')
					elseif escaped_byte == BYTE_BACKSLASH then table.insert(buffer, '\\')
					elseif escaped_byte == BYTE_DOUBLE_QUOTE then table.insert(buffer, '\"')
					elseif escaped_byte == BYTE_SINGLE_QUOTE then table.insert(buffer, "'" )
					-- Add other escape sequences as needed (e.g., \ddd, \xdd, \u{hhhh})
					else table.insert(buffer, string.char(escaped_byte)) 
					end
					current_pos = current_pos + 1
				elseif char_in_byte == quote_byte then
					current_pos = current_pos + 1 -- consume the closing quote
					break
				else
					table.insert(buffer, string.char(char_in_byte))
					current_pos = current_pos + 1
				end
			end
			table.insert(tokens, { "string", table.concat(buffer) })
			token_processed = true
		elseif char_byte == BYTE_LBRACKET then
			-- Check for long string: [[ ... ]] or [=[ ... ]=]
			local next_pos = current_pos + 1
			local num_equals = 0
			while next_pos <= len and code:byte(next_pos) == BYTE_EQUALS do
				num_equals = num_equals + 1
				next_pos = next_pos + 1
			end

			if code:byte(next_pos) == BYTE_LBRACKET then
				-- Confirmed long string start
				current_pos = next_pos + 1 -- Move past the opening [===[
				local content_start = current_pos
				
				-- Define the closing delimiter: ] followed by same num of =, then ]
				local expected_end = "]" .. string.rep("=", num_equals) .. "]"
				local end_len = #expected_end
				local closing_found = false

				while current_pos <= len do
					if code:sub(current_pos, current_pos + end_len - 1) == expected_end then
						local content_end = current_pos - 1
						local content = code:sub(content_start, content_end)

						-- Lua Rule: If the first character of the content is a newline, it's ignored
						if content:byte(1) == BYTE_LF then
							content = content:sub(2)
						elseif content:sub(1, 2) == "\r\n" then
							content = content:sub(3)
						end

						table.insert(tokens, { "string", content })
						current_pos = current_pos + end_len
						closing_found = true
						break
					end
					current_pos = current_pos + 1
				end

				if not closing_found then
					error("Unfinished long string near position " .. content_start)
				end
			else
				-- It's just a regular square bracket
				table.insert(tokens, { "square_bracket", "[" })
				current_pos = current_pos + 1
			end
			token_processed = true
		elseif char_byte == BYTE_RBRACKET then
			-- Regular square bracket
			table.insert(tokens, { "square_bracket", "]" })
			current_pos = current_pos + 1
			token_processed = true
		elseif char_byte == BYTE_LBRACE or char_byte == BYTE_RBRACE then
			table.insert(tokens, { "brace", string.char(char_byte) })
			current_pos = current_pos + 1
			token_processed = true
		elseif char_byte == BYTE_COLON then
			-- Check for label delimiter (::)
			if current_pos + 1 <= len and code:byte(current_pos + 1) == BYTE_COLON then
				table.insert(tokens, { "label_delimiter", "::" })
				current_pos = current_pos + 2
			else
				table.insert(tokens, { "colon", ":" })
				current_pos = current_pos + 1
			end
			token_processed = true
		elseif char_byte == BYTE_DOT then
			if current_pos + 2 <= len and code:sub(current_pos + 1, current_pos + 2) == '..' then
				table.insert(tokens, { "varargs", "..." })
				current_pos = current_pos + 3
			elseif current_pos + 1 <= len and code:byte(current_pos + 1) == BYTE_DOT then
				table.insert(tokens, { "operator", ".." })
				current_pos = current_pos + 2
			else
				table.insert(tokens, { "dot", "." })
				current_pos = current_pos + 1
			end
			token_processed = true
		elseif char_byte == BYTE_COMMA then
			table.insert(tokens, { "comma", "," })
			current_pos = current_pos + 1
			token_processed = true
		else
			-- For now, ignore other characters
			current_pos = current_pos + 1
			token_processed = true
		end
	end
	parser.position = current_pos
	return tokens
end

return Tokenizer
