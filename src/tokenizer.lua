-- Tokenizer module for LuaX Parser
-- Contains all tokenization logic extracted from translator.lua

local function is_digit(c)
	return c >= '0' and c <= '9'
end

local function is_alpha(c)
	return (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z') or c == '_'
end

local function is_whitespace(c)
	return c == ' ' or c == '\t' or c == '\n' or c == '\r'
end

local Tokenizer = {}

function Tokenizer.tokenize(parser)
	while parser.position <= #parser.code do
		local char = parser.code:sub(parser.position, parser.position)
		local token_processed = false 

		if is_whitespace(char) then
			parser.position = parser.position + 1
			token_processed = true
		elseif is_digit(char) then
			local start_pos = parser.position
			local is_float = false
			
			-- Check for hexadecimal literal
			local is_hex = false
			if char == '0' and parser.position + 1 <= #parser.code then
				local next_char = parser.code:sub(parser.position + 1, parser.position + 1)
				if next_char == 'x' or next_char == 'X' then
					is_hex = true
					parser.position = parser.position + 2 -- Consume '0x'
					local hex_start_pos = parser.position
					while parser.position <= #parser.code do
						local hex_char = parser.code:sub(parser.position, parser.position)
						if (hex_char >= '0' and hex_char <= '9') or
						   (hex_char >= 'a' and hex_char <= 'f') or
						   (hex_char >= 'A' and hex_char <= 'F') then
							parser.position = parser.position + 1
						else
							break
						end
					end
					local hex_val_str = parser.code:sub(hex_start_pos, parser.position - 1)
					if #hex_val_str == 0 then
						error("Malformed hexadecimal number")
					end
					-- Convert hex string to decimal number
					local decimal_val = tonumber(hex_val_str, 16)
					table.insert(parser.tokens, { type = "integer", value = decimal_val })
					token_processed = true
				end
			end

			if not is_hex then
				-- Original decimal number parsing
				while parser.position <= #parser.code do
					local current_char = parser.code:sub(parser.position, parser.position)
					if is_digit(current_char) then
						parser.position = parser.position + 1
					elseif current_char == '.' and not is_float then
						is_float = true
						parser.position = parser.position + 1
					else
						break
					end
				end
				local val = parser.code:sub(start_pos, parser.position - 1)
				if is_float then
					table.insert(parser.tokens, { type = "number", value = val })
				else
					table.insert(parser.tokens, { type = "integer", value = val })
				end
				token_processed = true
			end
			
		elseif is_alpha(char) then
			local start_pos = parser.position
			while parser.position <= #parser.code and (is_alpha(parser.code:sub(parser.position, parser.position)) or is_digit(parser.code:sub(parser.position, parser.position))) do
				parser.position = parser.position + 1
			end
			local value = parser.code:sub(start_pos, parser.position - 1)
			if value == "local" then table.insert(parser.tokens, { type = "keyword", value = "local" })
			elseif value == "function" then table.insert(parser.tokens, { type = "keyword", value = "function" })
			elseif value == "return" then table.insert(parser.tokens, { type = "keyword", value = "return" })
			elseif value == "end" then table.insert(parser.tokens, { type = "keyword", value = "end" })
			elseif value == "if" then table.insert(parser.tokens, { type = "keyword", value = "if" })
			elseif value == "then" then table.insert(parser.tokens, { type = "keyword", value = "then" })
			elseif value == "else" then table.insert(parser.tokens, { type = "keyword", value = "else" })
			elseif value == "elseif" then table.insert(parser.tokens, { type = "keyword", value = "elseif" })
			elseif value == "while" then table.insert(parser.tokens, { type = "keyword", value = "while" })
			elseif value == "for" then table.insert(parser.tokens, { type = "keyword", value = "for" })
			elseif value == "do" then table.insert(parser.tokens, { type = "keyword", value = "do" })
			elseif value == "and" or value == "or" or value == "not" then table.insert(parser.tokens, { type = "operator", value = value })
			elseif value == "goto" then table.insert(parser.tokens, { type = "keyword", value = "goto" })
			elseif value == "break" then table.insert(parser.tokens, { type = "keyword", value = "break" })
			elseif value == "repeat" then table.insert(parser.tokens, { type = "keyword", value = "repeat" })
			elseif value == "until" then table.insert(parser.tokens, { type = "keyword", value = "until" })
			else table.insert(parser.tokens, { type = "identifier", value = value }) end
			token_processed = true


		elseif char == '-' then
			if parser.position + 1 <= #parser.code and parser.code:sub(parser.position + 1, parser.position + 1) == '-' then
				-- Start of comment: consume '--'
				parser.position = parser.position + 2 
				
				local next_char = parser.code:sub(parser.position, parser.position)
				
				-- Check for long comment start: --[
				if next_char == '[' then
					parser.position = parser.position + 1 -- consume first '['
					local num_equals = 0
					-- Count equals signs
					while parser.position <= #parser.code and parser.code:sub(parser.position, parser.position) == '=' do
						num_equals = num_equals + 1
						parser.position = parser.position + 1
					end
					
					local second_bracket = parser.code:sub(parser.position, parser.position)
					
					if second_bracket == '[' then
						-- Confirmed multi-line comment: --[=[...]=]
						parser.position = parser.position + 1 -- consume second '['
						
						-- Determine expected closing sequence
						local expected_end = ']'
						for i = 1, num_equals do
							expected_end = expected_end .. '='
						end
						expected_end = expected_end .. ']'
						local end_len = #expected_end
						
						local comment_end_found = false
						while parser.position <= #parser.code do
							-- Check for the closing sequence, ensuring we don't index past the end
							if parser.position + end_len - 1 <= #parser.code then
								if parser.code:sub(parser.position, parser.position + end_len - 1) == expected_end then
									parser.position = parser.position + end_len
									comment_end_found = true
									break
								end
							end
							parser.position = parser.position + 1
						end
						
						if not comment_end_found then
							error("Unclosed multi-line comment")
						end
					else
						-- Not a valid long comment start (e.g., --[a or --[=a). Treat as single line.
						while parser.position <= #parser.code and parser.code:sub(parser.position, parser.position) ~= '\n' do
							parser.position = parser.position + 1
						end
					end
				else
					-- Standard single-line comment: --
					while parser.position <= #parser.code and parser.code:sub(parser.position, parser.position) ~= '\n' do
						parser.position = parser.position + 1
					end
				end
			else
				table.insert(parser.tokens, { type = "operator", value = char })
				parser.position = parser.position + 1
			end
			token_processed = true
		elseif char == '+' or char == '*' or char == '#' or char == '%' then
			table.insert(parser.tokens, { type = "operator", value = char })
			parser.position = parser.position + 1
			token_processed = true
		elseif char == '/' then
			-- Check for floor division //
			if parser.position < #parser.code and parser.code:sub(parser.position + 1, parser.position + 1) == '/' then
				table.insert(parser.tokens, { type = "operator", value = "//" })
				parser.position = parser.position + 2
			else
				table.insert(parser.tokens, { type = "operator", value = "/" })
				parser.position = parser.position + 1
			end
			token_processed = true
		elseif char == '&' then
			table.insert(parser.tokens, { type = "operator", value = "&" })
			parser.position = parser.position + 1
			token_processed = true
		elseif char == '|' then
			table.insert(parser.tokens, { type = "operator", value = "|" })
			parser.position = parser.position + 1
			token_processed = true
		elseif char == '=' then
			if parser.position < #parser.code and parser.code:sub(parser.position + 1, parser.position + 1) == '=' then
				table.insert(parser.tokens, { type = "operator", value = "==" })
				parser.position = parser.position + 2
			else
				table.insert(parser.tokens, { type = "operator", value = "=" })
				parser.position = parser.position + 1
			end
			token_processed = true
		elseif char == '>' then
			if parser.position < #parser.code and parser.code:sub(parser.position + 1, parser.position + 1) == '=' then
				table.insert(parser.tokens, { type = "operator", value = ">=" })
				parser.position = parser.position + 2
			elseif parser.position < #parser.code and parser.code:sub(parser.position + 1, parser.position + 1) == '>' then
				-- Right shift >>
				table.insert(parser.tokens, { type = "operator", value = ">>" })
				parser.position = parser.position + 2
			else
				table.insert(parser.tokens, { type = "operator", value = ">" })
				parser.position = parser.position + 1
			end
			token_processed = true
		elseif char == '<' then
			if parser.position < #parser.code and parser.code:sub(parser.position + 1, parser.position + 1) == '=' then
				table.insert(parser.tokens, { type = "operator", value = "<=" })
				parser.position = parser.position + 2
			elseif parser.position < #parser.code and parser.code:sub(parser.position + 1, parser.position + 1) == '<' then
				-- Left shift <<
				table.insert(parser.tokens, { type = "operator", value = "<<" })
				parser.position = parser.position + 2
			else
				table.insert(parser.tokens, { type = "operator", value = "<" })
				parser.position = parser.position + 1
			end
			token_processed = true
		elseif char == '~' then
			if parser.position < #parser.code and parser.code:sub(parser.position + 1, parser.position + 1) == '=' then
				table.insert(parser.tokens, { type = "operator", value = "~=" })
				parser.position = parser.position + 2
			else
				-- ~ is bitwise NOT (unary) or bitwise XOR (binary) in Lua 5.3+
				table.insert(parser.tokens, { type = "operator", value = "~" })
				parser.position = parser.position + 1
			end
			token_processed = true
		elseif char == '(' or char == ')' then
			table.insert(parser.tokens, { type = "paren", value = char })
			parser.position = parser.position + 1
			token_processed = true
		elseif char == '"' or char == "'" then
			local quote_char = char
			parser.position = parser.position + 1 -- consume the opening quote
			local buffer = {}
			local start_pos = parser.position
			while parser.position <= #parser.code do
				local char_in_string = parser.code:sub(parser.position, parser.position)
				if char_in_string == '\\' then
					parser.position = parser.position + 1 -- consume '\'
					local escaped_char = parser.code:sub(parser.position, parser.position)
					if escaped_char == 'n' then table.insert(buffer, '\n')
					elseif escaped_char == 't' then table.insert(buffer, '\t')
					elseif escaped_char == 'r' then table.insert(buffer, '\r')
					elseif escaped_char == 'b' then table.insert(buffer, '\b')
					elseif escaped_char == 'f' then table.insert(buffer, '\f')
					elseif escaped_char == 'a' then table.insert(buffer, '\a')
					elseif escaped_char == 'v' then table.insert(buffer, '\v')
					elseif escaped_char == '\\' then table.insert(buffer, '\\')
					elseif escaped_char == '"' then table.insert(buffer, '\"')
					elseif escaped_char == "'" then table.insert(buffer, "'" )
					-- Add other escape sequences as needed (e.g., \ddd, \xdd, \u{hhhh})
					else table.insert(buffer, escaped_char) -- For unknown escape sequences, just insert the char
					end
					parser.position = parser.position + 1
				elseif char_in_string == quote_char then
					parser.position = parser.position + 1 -- consume the closing quote
					break
				else
					table.insert(buffer, char_in_string)
					parser.position = parser.position + 1
				end
			end
			table.insert(parser.tokens, { type = "string", value = table.concat(buffer) })
			token_processed = true
		elseif char == '[' then
			-- Check for long string: [[ ... ]] or [=[ ... ]=]
			local next_pos = parser.position + 1
			local num_equals = 0
			while next_pos <= #parser.code and parser.code:sub(next_pos, next_pos) == '=' do
				num_equals = num_equals + 1
				next_pos = next_pos + 1
			end

			if parser.code:sub(next_pos, next_pos) == '[' then
				-- Confirmed long string start
				parser.position = next_pos + 1 -- Move past the opening [===[
				local content_start = parser.position

				-- Define the closing delimiter: ] followed by same num of =, then ]
				local expected_end = "]" .. string.rep("=", num_equals) .. "]"
				local end_len = #expected_end
				local closing_found = false

				while parser.position <= #parser.code do
					if parser.code:sub(parser.position, parser.position + end_len - 1) == expected_end then
						local content_end = parser.position - 1
						local content = parser.code:sub(content_start, content_end)

						-- Lua Rule: If the first character of the content is a newline, it's ignored
						if content:sub(1, 1) == "\n" then
							content = content:sub(2)
						elseif content:sub(1, 2) == "\r\n" then
							content = content:sub(3)
						end

						table.insert(parser.tokens, { type = "string", value = content })
						parser.position = parser.position + end_len
						closing_found = true
						break
					end
					parser.position = parser.position + 1
				end

				if not closing_found then
					error("Unfinished long string near position " .. content_start)
				end
			else
				-- It's just a regular square bracket
				table.insert(parser.tokens, { type = "square_bracket", value = "[" })
				parser.position = parser.position + 1
			end
			token_processed = true
		elseif char == ']' then
			-- Regular square bracket
			table.insert(parser.tokens, { type = "square_bracket", value = "]" })
			parser.position = parser.position + 1
			token_processed = true
		elseif char == '{' or char == '}' then
			table.insert(parser.tokens, { type = "brace", value = char })
			parser.position = parser.position + 1
			token_processed = true
		elseif char == ':' then
			-- Check for label delimiter (::)
			if parser.position + 1 <= #parser.code and parser.code:sub(parser.position + 1, parser.position + 1) == ':' then
				table.insert(parser.tokens, { type = "label_delimiter", value = "::" })
				parser.position = parser.position + 2
			else
				table.insert(parser.tokens, { type = "colon", value = char })
				parser.position = parser.position + 1
			end
			token_processed = true
		elseif char == '.' then
			if parser.position + 2 <= #parser.code and parser.code:sub(parser.position + 1, parser.position + 2) == '..' then
				table.insert(parser.tokens, { type = "varargs", value = "..." })
				parser.position = parser.position + 3
			elseif parser.position + 1 <= #parser.code and parser.code:sub(parser.position + 1, parser.position + 1) == '.' then
				table.insert(parser.tokens, { type = "operator", value = ".." })
				parser.position = parser.position + 2
			else
				table.insert(parser.tokens, { type = "dot", value = char })
				parser.position = parser.position + 1
			end
			token_processed = true
		elseif char == ',' then
			table.insert(parser.tokens, { type = "comma", value = char })
			parser.position = parser.position + 1
			token_processed = true
		else
			-- For now, ignore other characters
			parser.position = parser.position + 1
			token_processed = true
		end
	end
	return parser.tokens
end

return Tokenizer
