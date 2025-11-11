
-- Node class implementation
local Node = {}
Node.__index = Node

function Node:new(type, value, identifier)
    local instance = setmetatable({}, Node)
    instance.type = type
    instance.value = value
    instance.identifier = identifier
    instance.parent = nil
    instance.ordered_children = {}
    return instance
end

function Node:SetParent(parent)
    self.parent = parent
end

function Node:AddChildren(...)
    for _, child in ipairs({...}) do
        table.insert(self.ordered_children, child)
        child:SetParent(self)
    end
end

function Node:GenerateIterator(complete_stack)
    local stack = {}
    local function traverse(node)
        table.insert(stack, node)
        for _, child in ipairs(node.ordered_children) do
            traverse(child)
        end
    end

    if complete_stack then
        traverse(self)
        local i = 0
        return function()
            i = i + 1
            return stack[i]
        end
    else
        local i = 0
        local function getCurrentNodeChildren()
            i = i + 1
            return self.ordered_children[i]
        end
        return getCurrentNodeChildren
    end
end

-- Parser implementation
local Parser = {}
Parser.__index = Parser

function Parser:new(code)
    local instance = setmetatable({}, Parser)
    instance.code = code
    instance.position = 1
    instance.tokens = {}
    return instance
end

local function is_digit(c)
    return c >= '0' and c <= '9'
end

local function is_alpha(c)
    return (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z') or c == '_'
end

local function is_whitespace(c)
    return c == ' ' or c == '\t' or c == '\n' or c == '\r'
end

function Parser:tokenize()
    while self.position <= #self.code do
        local char = self.code:sub(self.position, self.position)

        if is_whitespace(char) then
            self.position = self.position + 1
        elseif is_digit(char) then
            local start_pos = self.position
            while self.position <= #self.code and is_digit(self.code:sub(self.position, self.position)) do
                self.position = self.position + 1
            end
            table.insert(self.tokens, { type = "number", value = self.code:sub(start_pos, self.position - 1) })
        elseif is_alpha(char) then
            local start_pos = self.position
            while self.position <= #self.code and (is_alpha(self.code:sub(self.position, self.position)) or is_digit(self.code:sub(self.position, self.position))) do
                self.position = self.position + 1
            end
            local value = self.code:sub(start_pos, self.position - 1)
            table.insert(self.tokens, { type = "identifier", value = value })
        elseif char == '+' or char == '-' or char == '*' or char == '/' or char == '=' then
            table.insert(self.tokens, { type = "operator", value = char })
            self.position = self.position + 1
        elseif char == '(' or char == ')' then
            table.insert(self.tokens, { type = "paren", value = char })
            self.position = self.position + 1
        elseif char == '"' or char == "'" then
            local start_pos = self.position
            local quote_char = char
            self.position = self.position + 1 -- consume the opening quote
            while self.position <= #self.code and self.code:sub(self.position, self.position) ~= quote_char do
                self.position = self.position + 1
            end
            if self.position <= #self.code then -- found closing quote
                self.position = self.position + 1 -- consume the closing quote
                table.insert(self.tokens, { type = "string", value = self.code:sub(start_pos + 1, self.position - 2) })
            else
                -- Error: unclosed string
                error("Unclosed string literal")
            end
        elseif char == '{' or char == '}' then
            table.insert(self.tokens, { type = "brace", value = char })
            self.position = self.position + 1
        elseif char == ':' then
            table.insert(self.tokens, { type = "colon", value = char })
            self.position = self.position + 1
        elseif char == '.' then
            table.insert(self.tokens, { type = "dot", value = char })
            self.position = self.position + 1
        else
            -- For now, ignore other characters
            self.position = self.position + 1
        end
    end
    return self.tokens
end

function Parser:peek()
    return self.tokens[self.token_position]
end

local precedence = {
    ['+'] = 1,
    ['-'] = 1,
    ['*'] = 2,
    ['/'] = 2,
}

function Parser:parse_function_call_or_member_access(base_node)
    local current_node = base_node
    while self:peek() and (self:peek().value == '(' or self:peek().type == "dot" or self:peek().type == "colon") do
        local token = self:peek()
        if token.value == '(' then
            -- Function call
            self.token_position = self.token_position + 1 -- consume '('
            local call_node = Node:new("call_expression")
            call_node:AddChildren(current_node) -- The function being called

            -- Parse arguments
            while self:peek() and self:peek().value ~= ')' do
                local arg = self:parse_expression()
                if arg then
                    call_node:AddChildren(arg)
                else
                    error("Expected argument in function call")
                end
                if self:peek() and self:peek().value == ',' then
                    self.token_position = self.token_position + 1 -- consume ','
                end
            end

            if self:peek() and self:peek().value == ')' then
                self.token_position = self.token_position + 1 -- consume ')'
            else
                error("Expected ')' in function call")
            end
            current_node = call_node
        elseif token.type == "dot" then
            -- Member access: base.member
            self.token_position = self.token_position + 1 -- consume '.'
            local member_token = self:peek()
            if member_token and member_token.type == "identifier" then
                self.token_position = self.token_position + 1
                local member_node = Node:new("member_expression")
                member_node:AddChildren(current_node, Node:new("identifier", member_token.value))
                current_node = member_node
            else
                error("Expected identifier after '.'")
            end
        elseif token.type == "colon" then
            -- Method call: base:method()
            self.token_position = self.token_position + 1 -- consume ':'
            local method_token = self:peek()
            if method_token and method_token.type == "identifier" then
                self.token_position = self.token_position + 1
                local method_call_node = Node:new("method_call_expression")
                method_call_node:AddChildren(current_node, Node:new("identifier", method_token.value))

                -- Expect '(' for arguments
                if self:peek() and self:peek().value == '(' then
                    self.token_position = self.token_position + 1 -- consume '('
                    while self:peek() and self:peek().value ~= ')' do
                        local arg = self:parse_expression()
                        if arg then
                            method_call_node:AddChildren(arg)
                        else
                            error("Expected argument in method call")
                        end
                        if self:peek() and self:peek().value == ',' then
                            self.token_position = self.token_position + 1 -- consume ','
                        end
                    end
                    if self:peek() and self:peek().value == ')' then
                        self.token_position = self.token_position + 1 -- consume ')'
                    else
                        error("Expected ')' in method call")
                    end
                else
                    error("Expected '(' after method identifier")
                end
                current_node = method_call_node
            else
                error("Expected identifier after ':'")
            end
        end
    end
    return current_node
end

function Parser:parse_table_constructor()
    self.token_position = self.token_position + 1 -- consume '{' 
    local table_node = Node:new("table_constructor")

    while self:peek() and self:peek().value ~= '}' do
        local key_node = nil
        local value_node = nil

        local current_token = self:peek()
        if current_token.type == "identifier" and self.tokens[self.token_position + 1] and self.tokens[self.token_position + 1].value == '=' then
            -- Record-style field: identifier = expression
            key_node = Node:new("identifier", current_token.value)
            self.token_position = self.token_position + 2 -- consume identifier and '='
            value_node = self:parse_expression()
        else
            -- List-style field: expression
            value_node = self:parse_expression()
        end

        if value_node then
            local field_node = Node:new("table_field")
            if key_node then
                field_node:AddChildren(key_node, value_node)
            else
                field_node:AddChildren(value_node)
            end
            table_node:AddChildren(field_node)
        end

        -- Consume comma if present
        if self:peek() and self:peek().value == ',' then
            self.token_position = self.token_position + 1
        end
    end

    if self:peek() and self:peek().value == '}' then
        self.token_position = self.token_position + 1 -- consume '}'
    else
        error("Expected '}' in table constructor")
    end

    return table_node
end

function Parser:parse_primary_expression()
    local token = self:peek()
    if not token then return nil end

    local node = nil
    if token.type == "number" or token.type == "string" then
        self.token_position = self.token_position + 1
        node = Node:new(token.type, token.value)
    elseif token.type == "identifier" then
        self.token_position = self.token_position + 1
        node = Node:new(token.type, token.value, token.value)
    elseif token.value == '(' then
        self.token_position = self.token_position + 1 -- consume '('
        local expr = self:parse_expression()
        local next_token = self:peek()
        if next_token and next_token.value == ')' then
            self.token_position = self.token_position + 1 -- consume ')'
            node = expr
        else
            error("Expected ')'")
        end
    elseif token.value == '{' then
        node = self:parse_table_constructor()
    else
        return nil
    end

    if node then
        return self:parse_function_call_or_member_access(node)
    end
    return nil
end

function Parser:parse_expression()
    local output_queue = {}
    local operator_stack = {}

    local function get_top_operator()
        return operator_stack[#operator_stack]
    end

    while self.token_position <= #self.tokens do
        local token = self.tokens[self.token_position]
        if not token then break end

        if token.type == "number" or token.type == "identifier" or token.type == "string" or token.value == '(' or token.value == '{' then
            local primary_expr_node = self:parse_primary_expression()
            if primary_expr_node then
                table.insert(output_queue, primary_expr_node)
            else
                error("Unexpected token in expression: " .. token.value)
            end
        elseif token.type == "operator" then
            while #operator_stack > 0 and get_top_operator().value ~= '(' and
                  (precedence[get_top_operator().value] and precedence[token.value] and
                  (precedence[get_top_operator().value] > precedence[token.value] or
                  (precedence[get_top_operator().value] == precedence[token.value]))) do
                table.insert(output_queue, table.remove(operator_stack))
            end
            table.insert(operator_stack, token)
            self.token_position = self.token_position + 1
        elseif token.value == ')' then
            while #operator_stack > 0 and get_top_operator().value ~= '(' do
                table.insert(output_queue, table.remove(operator_stack))
            end
            table.remove(operator_stack) -- Pop the '('
            self.token_position = self.token_position + 1
        elseif token.value == ')' or token.value == ',' then
            -- End of expression or argument separator
            break
        else
            -- End of expression
            break
        end
    end

    while #operator_stack > 0 do
        table.insert(output_queue, table.remove(operator_stack))
    end

    -- Build the expression tree from the RPN
    local expression_stack = {}
    for _, item in ipairs(output_queue) do
        if type(item) == "table" and item.type ~= "operator" then -- It's an AST node (number, identifier, string, table, call_expression, member_expression, etc.)
            table.insert(expression_stack, item)
        elseif type(item) == "table" and item.type == "operator" then -- It's an operator token
            local right_node = table.remove(expression_stack)
            local left_node = table.remove(expression_stack)
            local expression_node = Node:new("binary_expression", item.value)
            expression_node:AddChildren(left_node, right_node)
            table.insert(expression_stack, expression_node)
        end
    end

    return expression_stack[1]
end

function Parser:parse_statement()
    local current_token = self:peek()
    if not current_token then
        return nil
    end

    if current_token.type == "identifier" and self.tokens[self.token_position + 1] and self.tokens[self.token_position + 1].value == '=' then
        local identifier_token = self.tokens[self.token_position]
        self.token_position = self.token_position + 2 -- consume identifier and '='
        
        local expression_node = self:parse_expression()
        if not expression_node then
            -- Handle error: unexpected end of input
            return nil
        end

        local assignment_node = Node:new("assignment", nil, nil)
        local variable_node = Node:new("variable", nil, identifier_token.value)
        
        assignment_node:AddChildren(variable_node, expression_node)
        return assignment_node
    elseif current_token.type == "identifier" and current_token.value == "local" then
        self.token_position = self.token_position + 1 -- consume 'local'
        local identifier_token = self:peek()
        if not identifier_token or identifier_token.type ~= "identifier" then
            error("Expected identifier after 'local'")
        end
        self.token_position = self.token_position + 1 -- consume identifier

        local local_declaration_node = Node:new("local_declaration")
        local variable_node = Node:new("variable", nil, identifier_token.value)
        local_declaration_node:AddChildren(variable_node)

        if self:peek() and self:peek().value == '=' then
            self.token_position = self.token_position + 1 -- consume '='
            local expression_node = self:parse_expression()
            if expression_node then
                local_declaration_node:AddChildren(expression_node)
            else
                error("Expected expression after '=' in local declaration")
            end
        end
        return local_declaration_node
    else
        -- Try to parse as a standalone expression (e.g., function call)
        local expression_node = self:parse_expression()
        if expression_node then
            local expression_statement_node = Node:new("expression_statement")
            expression_statement_node:AddChildren(expression_node)
            return expression_statement_node
        end
    end
    
    return nil
end

function Parser:parse()
    self:tokenize()
    self.token_position = 1
    local root = Node:new("Root", nil, nil)
    
    while self.token_position <= #self.tokens do
        local statement_node = self:parse_statement()
        if statement_node then
            root:AddChildren(statement_node)
        else
            -- For now, just advance the token position if a statement can't be parsed
            self.token_position = self.token_position + 1
        end
    end

    return root
end


-- Translator function
function translate(code)
    local parser = Parser:new(code)
    return parser:parse()
end

return {
    Node = Node,
    translate = translate,
    Parser = Parser
}
