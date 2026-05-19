-- Simple C++ Code Formatter
-- Provides basic formatting for generated C++ code without external dependencies
-- Optimized: uses string.byte() for character inspection to avoid temporary string allocations

local Formatter = {}

-- Pre-computed byte constants
local BYTE_BACKSLASH = 92   -- byte('\\')
local BYTE_DQUOTE    = 34   -- byte('"')
local BYTE_SQUOTE    = 39   -- byte("'")
local BYTE_SEMICOLON = 59   -- byte(';')
local BYTE_LBRACE    = 123  -- byte('{')
local BYTE_RBRACE    = 125  -- byte('}')
local BYTE_SPACE     = 32   -- byte(' ')
local BYTE_TAB       = 9    -- byte('\t')
local BYTE_LF        = 10   -- byte('\n')
local BYTE_CR        = 13   -- byte('\r')

local byte = string.byte
local sub = string.sub

-- Format C++ code by splitting multi-statement lines and adding proper indentation
function Formatter.format_cpp_code(code)
	local lines = {}
	
	-- First, split the code into lines
	for line in (code .. "\n"):gmatch("([^\n]*)\n") do
		table.insert(lines, line)
	end
	
	local formatted_lines = {}
	local indent_level = 0
	local indent_str = "\t"
	
	for _, line in ipairs(lines) do
		-- Trim whitespace
		local trimmed = line:match("^%s*(.-)%s*$")
		
		if trimmed ~= "" then
			local trimmed_len = #trimmed
			-- Check if this line contains multiple statements (multiple semicolons)
			local statements = {}
			local in_string = false
			local in_char = false
			local escape_next = false
			local stmt_start = 1
			
			local trimmed_bytes = { byte(trimmed, 1, trimmed_len) }
			for i = 1, trimmed_len do
				local b = trimmed_bytes[i]
				
				if escape_next then
					escape_next = false
				elseif b == BYTE_BACKSLASH then
					escape_next = true
				elseif b == BYTE_DQUOTE and not in_char then
					in_string = not in_string
				elseif b == BYTE_SQUOTE and not in_string then
					in_char = not in_char
				elseif b == BYTE_SEMICOLON and not in_string and not in_char then
					-- Extract the statement including the semicolon
					local stmt = sub(trimmed, stmt_start, i)
					-- Check if this is a meaningful statement (not just whitespace)
					if stmt:match("%S") then
						table.insert(statements, stmt)
					end
					stmt_start = i + 1
				end
			end
			
			-- Add any remaining content
			if stmt_start <= trimmed_len then
				local remaining = sub(trimmed, stmt_start)
				if remaining:match("%S") then
					table.insert(statements, remaining)
				end
			end
			
			-- Process each statement
			for _, stmt in ipairs(statements) do
				local stmt_trimmed = stmt:match("^%s*(.-)%s*$")
				local stmt_len = #stmt_trimmed
				
				-- Pre-unpack statement bytes to avoid calling byte() inside loops
				local stmt_bytes = { byte(stmt_trimmed, 1, stmt_len) }
				
				-- Adjust indent level based on braces
				-- Count closing braces at the start to dedent before printing
				local leading_close_braces = 0
				for i = 1, stmt_len do
					local b = stmt_bytes[i]
					if b == BYTE_RBRACE then
						leading_close_braces = leading_close_braces + 1
					elseif b ~= BYTE_SPACE and b ~= BYTE_TAB then
						break
					end
				end
				
				indent_level = math.max(0, indent_level - leading_close_braces)
				
				-- Add the formatted statement
				local formatted_stmt = string.rep(indent_str, indent_level) .. stmt_trimmed
				table.insert(formatted_lines, formatted_stmt)
				
				-- Count braces to adjust indent for next line
				local open_braces = 0
				local close_braces = 0
				local in_str = false
				local in_ch = false
				local esc = false
				
				for i = 1, stmt_len do
					local b = stmt_bytes[i]
					
					if esc then
						esc = false
					elseif b == BYTE_BACKSLASH then
						esc = true
					elseif b == BYTE_DQUOTE and not in_ch then
						in_str = not in_str
					elseif b == BYTE_SQUOTE and not in_str then
						in_ch = not in_ch
					elseif not in_str and not in_ch then
						if b == BYTE_LBRACE then
							open_braces = open_braces + 1
						elseif b == BYTE_RBRACE then
							close_braces = close_braces + 1
						end
					end
				end
				
				-- Adjust indent level for next line (account for non-leading braces)
				indent_level = math.max(0, indent_level + open_braces - (close_braces - leading_close_braces))
			end
		else
			-- Preserve empty lines
			table.insert(formatted_lines, "")
		end
	end
	
	return table.concat(formatted_lines, "\n")
end

return Formatter