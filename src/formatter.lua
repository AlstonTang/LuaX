-- Simple C++ Code Formatter
-- Provides basic formatting for generated C++ code without external dependencies

local Formatter = {}

-- Format C++ code by splitting multi-statement lines and adding proper indentation
function Formatter.format_cpp_code(code)
	local lines = {}
	
	-- First, split the code into lines
	for line in code:gmatch("[^\n]*") do
		table.insert(lines, line)
	end
	
	local formatted_lines = {}
	local indent_level = 0
	local indent_str = "	"
	
	for _, line in ipairs(lines) do
		-- Trim whitespace
		local trimmed = line:match("^%s*(.-)%s*$")
		
		if trimmed ~= "" then
			-- Check if this line contains multiple statements (multiple semicolons)
			local statements = {}
			local in_string = false
			local in_char = false
			local escape_next = false
			local current_statement = ""
			
			for i = 1, #trimmed do
				local char = trimmed:sub(i, i)
				
				if escape_next then
					current_statement = current_statement .. char
					escape_next = false
				elseif char == "\\" then
					current_statement = current_statement .. char
					escape_next = true
				elseif char == '"' and not in_char then
					in_string = not in_string
					current_statement = current_statement .. char
				elseif char == "'" and not in_string then
					in_char = not in_char
					current_statement = current_statement .. char
				elseif char == ";" and not in_string and not in_char then
					current_statement = current_statement .. char
					-- Check if this is a meaningful statement (not just whitespace)
					if current_statement:match("%S") then
						table.insert(statements, current_statement)
					end
					current_statement = ""
				else
					current_statement = current_statement .. char
				end
			end
			
			-- Add any remaining content
			if current_statement:match("%S") then
				table.insert(statements, current_statement)
			end
			
			-- Process each statement
			for _, stmt in ipairs(statements) do
				local stmt_trimmed = stmt:match("^%s*(.-)%s*$")
				
				-- Adjust indent level based on braces
				-- Count closing braces at the start to dedent before printing
				local leading_close_braces = 0
				for i = 1, #stmt_trimmed do
					local char = stmt_trimmed:sub(i, i)
					if char == "}" then
						leading_close_braces = leading_close_braces + 1
					elseif char:match("%S") then
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
				
				for i = 1, #stmt_trimmed do
					local char = stmt_trimmed:sub(i, i)
					
					if esc then
						esc = false
					elseif char == "\\" then
						esc = true
					elseif char == '"' and not in_ch then
						in_str = not in_str
					elseif char == "'" and not in_str then
						in_ch = not in_ch
					elseif not in_str and not in_ch then
						if char == "{" then
							open_braces = open_braces + 1
						elseif char == "}" then
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