local Node = require("src.node")
local Tokenizer = require("src.tokenizer")

-- Parser implementation
local Parser = {}
Parser.__index = Parser

function Parser:new(code)
    local instance = setmetatable({}, Parser)
    instance.code = code
    instance.position = 1
    instance.tokens = {}
    
    -- Label Scoping Initialization
    math.randomseed(os.time() + (#code * 100)) -- Simple seed based on time and code length
    instance.label_scope = {} -- Current map of raw_name -> unique_name
    instance.label_scope_stack = {} -- Stack to handle nested functions

    return instance
end

-- Helper to handle unique label generation/retrieval within the current scope
function Parser:get_unique_label(raw_name)
    if not (self.label_scope[raw_name]) then
        local unique_name = raw_name .. "_" .. tostring(math.random(100000))
        self.label_scope[raw_name] = unique_name
    end
    return self.label_scope[raw_name]
end

function Parser:push_label_scope()
    -- Deep copy current scope to stack
    local scope_copy = {}
    for k, v in pairs(self.label_scope) do
        scope_copy[k] = v
    end
    table.insert(self.label_scope_stack, scope_copy)
    -- Reset current scope for new function/block
    self.label_scope = {}
end

function Parser:pop_label_scope()
    if #self.label_scope_stack > 0 then
        self.label_scope = table.remove(self.label_scope_stack)
    else
        self.label_scope = {}
    end
end

function Parser:tokenize()
    self.tokens = Tokenizer.tokenize(self)
    return self.tokens
end

function Parser:peek()
    return self.tokens[self.token_position]
end

local precedence = {
    ['or'] = 1,
    ['and'] = 2,
    ['<'] = 3, ['>'] = 3, ['<='] = 3, ['>='] = 3, ['~='] = 3, ['=='] = 3,
    ['|'] = 4,   -- bitwise OR
    ['~'] = 5,   -- bitwise XOR (binary)
    ['&'] = 6,   -- bitwise AND
    ['<<'] = 7, ['>>'] = 7,  -- bitwise shifts
    ['..'] = 8,
    ['+'] = 9,
    ['-'] = 9,
    ['*'] = 10,
    ['/'] = 10,
    ['//'] = 10,  -- floor division
    ['%'] = 10,   -- modulo
    ['^'] = 12,   -- exponentiation (right associative, highest)
}

function Parser:parse_function_call_or_member_access(base_node)
    local current_node = base_node
    while true do
        local token = self:peek() -- Peek at the current token to decide what to do

        if not token then break end -- No more tokens

        if token.value == '(' then
            -- Function call logic
            self.token_position = self.token_position + 1 -- consume '('
            local call_node = Node:new("call_expression")
            call_node:AddChildren(current_node)

            while self:peek() and self:peek().value ~= ')' do
                local arg = self:parse_expression(0)
                if arg then call_node:AddChildren(arg) else error("Expected argument") end
                if self:peek() and self:peek().value == ',' then self.token_position = self.token_position + 1 end
            end

            if self:peek() and self:peek().value == ')' then
                self.token_position = self.token_position + 1 -- consume ')'
            else error("Expected ')'") end
            current_node = call_node
            -- No need to update current_token here, the loop will re-peek

        elseif token.type == "dot" then
            -- Member access: base.member
            self.token_position = self.token_position + 1 -- consume '.'
            local member_token = self:peek()
            if member_token and member_token.type == "identifier" then
                self.token_position = self.token_position + 1 -- consume identifier
                local member_node = Node:new("member_expression")
                member_node:AddChildren(current_node, Node:new("identifier", member_token.value, member_token.value))
                current_node = member_node
                -- No need to update current_token here, the loop will re-peek
            else
                local found_token_type = member_token and member_token.type or "nil"
                local found_token_value = member_token and member_token.value or "nil"
                error("Expected identifier after '.' but found token type: " .. found_token_type .. " with value: " .. found_token_value)
            end

        elseif token.type == "colon" then
            -- Method call: base:method()
            self.token_position = self.token_position + 1 -- consume ':'
            local method_token = self:peek()
            if method_token and method_token.type == "identifier" then
                self.token_position = self.token_position + 1 -- consume method identifier
                local method_call_node = Node:new("method_call_expression")
                method_call_node:AddChildren(current_node, Node:new("identifier", method_token.value, method_token.value))

                local open_paren_token = self:peek()
                if open_paren_token and open_paren_token.value == '(' then
                    self.token_position = self.token_position + 1 -- consume '('
                    while self:peek() and self:peek().value ~= ')' do
                        local arg = self:parse_expression(0)
                        if arg then method_call_node:AddChildren(arg) else error("Expected argument") end
                        if self:peek() and self:peek().value == ',' then self.token_position = self.token_position + 1 end
                    end
                    local close_paren_token = self:peek()
                    if close_paren_token and close_paren_token.value == ')' then
                        self.token_position = self.token_position + 1 -- consume ')'
                    else error("Expected ')'") end
                else error("Expected '(' after method identifier") end
                current_node = method_call_node
            else 
                local val = method_token and method_token.value or "nil"
                local typ = method_token and method_token.type or "nil"
                error("Expected identifier after ':'") 
            end
        elseif token.type == "square_bracket" and token.value == '[' then
            self.token_position = self.token_position + 1 -- consume '['
            local index_expr = self:parse_expression(0)
            if not index_expr then error("Expected index expression inside '[]'") end

            local close_bracket_token = self:peek()
            if not close_bracket_token or not (close_bracket_token.type == "square_bracket" and close_bracket_token.value == ']') then
                error("Expected ']' to close table index")
            end
            self.token_position = self.token_position + 1 -- consume ']'

            local index_node = Node:new("table_index_expression")
            index_node:AddChildren(current_node, index_expr)
            current_node = index_node
        else
            -- If the current token is not '(', '.', ':', or '[', break the loop
            break
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
            key_node = Node:new("identifier", current_token.value, current_token.value) -- Set identifier field
            self.token_position = self.token_position + 2 -- consume identifier and '='
            value_node = self:parse_expression(0)
        elseif current_token.type == "square_bracket" and current_token.value == '[' then
            -- Explicit key-value field: [expression] = expression
            self.token_position = self.token_position + 1 -- consume '['
            key_node = self:parse_expression(0) -- Parse the key expression
            if not key_node then error("Expected key expression inside '[]'") end

            local close_bracket_token = self:peek()
            if not close_bracket_token or not (close_bracket_token.type == "square_bracket" and close_bracket_token.value == ']') then
                error("Expected ']' to close table key")
            end
            self.token_position = self.token_position + 1 -- consume ']'

            local equals_token = self:peek()
            if not equals_token or equals_token.value ~= '=' then
                error("Expected '=' after table key")
            end
            self.token_position = self.token_position + 1 -- consume '='

            value_node = self:parse_expression(0) -- Parse the value expression
        else
            -- List-style field: expression
            value_node = self:parse_expression(0)
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

function Parser:parse_variable_list()
    local variables = {}
    -- Parse the first variable
    local var_node = self:parse_function_call_or_member_access(self:parse_primary_expression())
    if not var_node then error("Expected variable in list") end
    table.insert(variables, var_node)

    -- Parse subsequent variables if separated by commas
    while self:peek() and self:peek().type == "comma" do
        self.token_position = self.token_position + 1 -- consume ','
        var_node = self:parse_function_call_or_member_access(self:parse_primary_expression())
        if not var_node then error("Expected variable in list after comma") end
        table.insert(variables, var_node)
    end

    local var_list_node = Node:new("variable_list")

    var_list_node:AddChildren(table.unpack(variables))
    return var_list_node
end

function Parser:parse_expression_list()
    local expressions = {}
    repeat
        local expr_node = self:parse_expression()
        if not expr_node then break end -- Allow empty expression list if no more expressions
        table.insert(expressions, expr_node)
        if self:peek() and self:peek().type == "comma" then
            self.token_position = self.token_position + 1 -- consume ','
        else
            break
        end
    until false
    local expr_list_node = Node:new("expression_list")
    for i = 1, #expressions do
        expr_list_node:AddChildren(expressions[i])
    end
    return expr_list_node
end

function Parser:parse_primary_expression()
    local token = self:peek()
    if not token then
        return nil
    end

    local node = nil
    if token.type == "operator" and token.value == '-' then -- Handle unary minus
        self.token_position = self.token_position + 1 -- consume '-'
        local operand = self:parse_expression(11) -- Unary ops have high precedence
        if not operand then 
            local next_tok = self:peek()
            local val = next_tok and next_tok.value or "nil"
            local typ = next_tok and next_tok.type or "nil"
            error("Expected expression after unary minus") 
        end
        node = Node:new("unary_expression", "-")
        node:AddChildren(operand)
    elseif token.type == "operator" and token.value == 'not' then -- Handle unary not
        self.token_position = self.token_position + 1 -- consume 'not'
        local operand = self:parse_expression(11) -- Unary ops have high precedence
        if not operand then error("Expected expression after unary not") end
        node = Node:new("unary_expression", "not")
        node:AddChildren(operand)
    elseif token.type == "operator" and token.value == '~' then -- Handle unary bitwise NOT
        self.token_position = self.token_position + 1 -- consume '~'
        local operand = self:parse_expression(11) -- Unary ops have high precedence
        if not operand then error("Expected expression after unary ~") end
        node = Node:new("unary_expression", "~")
        node:AddChildren(operand)
    elseif token.type == "operator" and token.value == '#' then -- Handle unary length
        self.token_position = self.token_position + 1 -- consume '#'
        local operand = self:parse_expression(11)
        if not operand then error("Expected expression after unary #") end
        node = Node:new("unary_expression", "#")
        node:AddChildren(operand)
    elseif token.type == "number" or token.type == "string" or token.type == "integer" then
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
    elseif token.type == "keyword" and token.value == "function" then
        self.token_position = self.token_position + 1 -- consume 'function'
        node = self:parse_function_body_content()
    elseif token.type == "varargs" then
        self.token_position = self.token_position + 1 -- consume ...
        node = Node:new("varargs", "...", "...")
    else
        return nil
    end

    if node then
        return node
    end
    return nil
end

function Parser:parse_expression(min_precedence)
    min_precedence = min_precedence or 0
    local left_expr = self:parse_primary_expression()
    if not left_expr then 
        error("parse_expression: parse_primary_expression returned nil. Current token: type=" .. (self:peek() and self:peek().type or "nil") .. ", value=" .. (self:peek() and self:peek().value or "nil"))
    end

    left_expr = self:parse_function_call_or_member_access(left_expr)

    while true do
        local operator_token = self:peek()
        if not operator_token or operator_token.type ~= "operator" then
            break
        end

        local op_precedence = precedence[operator_token.value]
        if not op_precedence or op_precedence < min_precedence then
            break
        end

        self.token_position = self.token_position + 1 -- consume operator
        local right_expr = self:parse_expression(op_precedence + 1) -- Recursive call with higher precedence
        if not right_expr then error("Expected expression after operator") end

        local binary_expr = Node:new("binary_expression", operator_token.value)
        binary_expr:AddChildren(left_expr, right_expr)
        left_expr = binary_expr
    end

    return left_expr
end

function Parser:parse_statement()
    local current_token = self:peek()
    if not current_token then
        return nil
    end

    if current_token.type == "keyword" and current_token.value == "local" then
        self.token_position = self.token_position + 1 -- consume 'local'
        local next_token_after_local = self:peek()
        if next_token_after_local and next_token_after_local.type == "keyword" and next_token_after_local.value == "function" then
            -- Handle local function declaration
            self.token_position = self.token_position + 1 -- consume 'function'
            local statement_node = self:parse_function_declaration(true) -- true for local function
            return statement_node
        else
            local local_declaration_node = Node:new("local_declaration")
            local var_list_node = self:parse_variable_list()
            local_declaration_node:AddChildren(var_list_node)

            if self:peek() and self:peek().value == '=' then
                self.token_position = self.token_position + 1 -- consume '='
                local expr_list_node = self:parse_expression_list()
                local_declaration_node:AddChildren(expr_list_node)
            end
            return local_declaration_node
        end
    elseif current_token.type == "keyword" and current_token.value == "if" then
        local statement_node = self:parse_if_statement()
        return statement_node
    elseif current_token.type == "keyword" and current_token.value == "while" then
        local statement_node = self:parse_while_statement()
        return statement_node
    elseif current_token.type == "keyword" and current_token.value == "for" then
        local statement_node = self:parse_for_statement()
        return statement_node
    elseif current_token.type == "keyword" and current_token.value == "function" then
        local next_token = self.tokens[self.token_position + 1]
        if next_token and next_token.type == "identifier" then
            -- This is a named function declaration
            self.token_position = self.token_position + 1 -- consume 'function'
            local statement_node = self:parse_function_declaration(false) -- false for global function
            return statement_node
        else
            -- This is an anonymous function, let parse_expression handle it
            -- Do not consume 'function' token here
            return nil -- Let the expression parsing handle it
        end
    elseif current_token.type == "keyword" and current_token.value == "return" then
        self.token_position = self.token_position + 1 -- consume 'return'
        local return_node = Node:new("return_statement")
        local expr_list = self:parse_expression_list() -- Return can have multiple expressions
        if #expr_list.ordered_children > 0 then
            return_node:AddChildren(expr_list)
        end
        return return_node
    elseif current_token.type == "label_delimiter" then
        -- Label: ::label_name::
        self.token_position = self.token_position + 1 -- consume first '::'
        local label_name_token = self:peek()
        if not label_name_token or label_name_token.type ~= "identifier" then
            error("Expected label name after '::'")
        end
        self.token_position = self.token_position + 1 -- consume label name
        local closing_delimiter = self:peek()
        if not closing_delimiter or closing_delimiter.type ~= "label_delimiter" then
            error("Expected '::' to close label")
        end
        self.token_position = self.token_position + 1 -- consume closing '::'
        
        -- Generate unique name for this scope
        local unique_name = self:get_unique_label(label_name_token.value)
        
        -- For now, just create a label node (will be ignored in C++ translation)
        local label_node = Node:new("label_statement", unique_name)
        return label_node
    elseif current_token.type == "keyword" and current_token.value == "break" then
        -- Break statement
        self.token_position = self.token_position + 1 -- consume 'break'
        local break_node = Node:new("break_statement")
        return break_node
    elseif current_token.type == "keyword" and current_token.value == "repeat" then
        -- Repeat-until loop: repeat <block> until <condition>
        self.token_position = self.token_position + 1 -- consume 'repeat'
        local repeat_node = Node:new("repeat_until_statement")
        
        -- Parse the block
        local block_node = Node:new("block")
        while self:peek() and not (self:peek().type == "keyword" and self:peek().value == "until") do
            local stmt = self:parse_statement()
            if stmt then
                block_node:AddChildren(stmt)
            else
                break
            end
        end
        
        -- Expect 'until'
        local until_token = self:peek()
        if not until_token or until_token.value ~= "until" then
            error("Expected 'until' to close repeat loop")
        end
        self.token_position = self.token_position + 1 -- consume 'until'
        
        -- Parse the condition
        local condition_expr = self:parse_expression(0)
        if not condition_expr then
            error("Expected condition after 'until'")
        end
        
        repeat_node:AddChildren(block_node, condition_expr)
        return repeat_node
    elseif current_token.type == "keyword" and current_token.value == "goto" then
        -- Goto: goto label_name
        self.token_position = self.token_position + 1 -- consume 'goto'
        local goto_target_token = self:peek()
        if not goto_target_token or goto_target_token.type ~= "identifier" then
            error("Expected label name after 'goto'")
        end
        self.token_position = self.token_position + 1 -- consume label name
        
        -- Get/Generate unique name for this scope
        local unique_name = self:get_unique_label(goto_target_token.value)

        -- For now, just create a goto node (will be translated to C++)
        local goto_node = Node:new("goto_statement", unique_name)
        return goto_node
    elseif current_token.type == "keyword" and current_token.value == "end" then
        -- 'end' is a block terminator, not a statement itself. Handle it by returning nil
        -- The block parsing logic will consume it.
        return nil
    else
        -- This could be an assignment (a=1 or a,b=1,2) or an expression statement (myfunc()).
        -- First, we parse a list of potential l-values (left-hand side variables).
        local lvalue_list_node = self:parse_variable_list()
        
        -- If nothing could be parsed as a variable/expression, it's not a statement.
        if #lvalue_list_node.ordered_children == 0 then
            return nil
        end

        -- Now, we check if an '=' follows the list.
        if self:peek() and self:peek().value == '=' then
            -- It's an assignment statement.
            self.token_position = self.token_position + 1 -- consume '='
            
            -- Now parse the r-values (right-hand side expressions).
            local rvalue_list_node = self:parse_expression_list()
            
            local assignment_node = Node:new("assignment")
            assignment_node:AddChildren(lvalue_list_node, rvalue_list_node)
            return assignment_node
        else
            -- It's not an assignment. It must be a standalone expression statement.
            -- This is only valid if the "variable list" we parsed contained exactly one item.
            if #lvalue_list_node.ordered_children == 1 then
                local expression_statement_node = Node:new("expression_statement")
                -- The single item from the list is the expression itself.
                expression_statement_node:AddChildren(lvalue_list_node.ordered_children[1])
                return expression_statement_node
            else
                -- This is a syntax error, e.g., "a, b" on a line by itself.
                error("Invalid statement: unexpected symbol near ','")
            end
        end
    end
    return nil
end

function Parser:parse_for_statement()
    self.token_position = self.token_position + 1 -- consume 'for'
    
    local first_identifier_token = self:peek()
    if not first_identifier_token or first_identifier_token.type ~= "identifier" then
        error("Expected identifier after 'for'")
    end
    self.token_position = self.token_position + 1 -- consume first identifier

    local next_token = self:peek()

    if next_token and next_token.value == '=' then
        -- Numeric for loop: for var = start, end, [step]
        local for_node = Node:new("for_numeric_statement")
        local variable_node = Node:new("identifier", first_identifier_token.value, first_identifier_token.value)
        for_node:AddChildren(variable_node)

        self.token_position = self.token_position + 1 -- consume '='

        local start_expr = self:parse_expression()
        if not start_expr then error("Expected start expression in numeric for loop") end
        for_node:AddChildren(start_expr)

        local comma_token = self:peek()
        if not comma_token or comma_token.type ~= "comma" then
            error("Expected ',' after numeric for loop start expression")
        end
        self.token_position = self.token_position + 1 -- consume ','

        local end_expr = self:parse_expression()
        if not end_expr then error("Expected end expression in numeric for loop") end
        for_node:AddChildren(end_expr)

        if self:peek() and self:peek().type == "comma" then
            self.token_position = self.token_position + 1 -- consume ','
            local step_expr = self:parse_expression()
            if not step_expr then error("Expected step expression in numeric for loop") end
            for_node:AddChildren(step_expr)
        end

        local do_token = self:peek()
        if not do_token or do_token.value ~= "do" then error("Expected 'do' after numeric for loop expressions") end
        self.token_position = self.token_position + 1 -- consume 'do'

        local body_node = Node:new("block")
        while self:peek() and self:peek().value ~= "end" do
            local statement = self:parse_statement()
            if statement then body_node:AddChildren(statement) end
        end

        local end_token = self:peek()
        if not end_token or end_token.value ~= "end" then error("Expected 'end' to close numeric for statement") end
        self.token_position = self.token_position + 1 -- consume 'end'

        for_node:AddChildren(body_node)
        return for_node
    elseif next_token and (next_token.type == "comma" or next_token.value == "in") then
        -- Generic for loop: for var_list in expr_list
        local for_node = Node:new("for_generic_statement")
        local var_list_node = Node:new("variable_list")
        var_list_node:AddChildren(Node:new("identifier", first_identifier_token.value, first_identifier_token.value))

        while self:peek() and self:peek().type == "comma" do
            self.token_position = self.token_position + 1 -- consume ','
            local identifier_token = self:peek()
            if not identifier_token or identifier_token.type ~= "identifier" then
                error("Expected identifier in generic for loop variable list")
            end
            self.token_position = self.token_position + 1 -- consume identifier
            var_list_node:AddChildren(Node:new("identifier", identifier_token.value, identifier_token.value))
        end
        for_node:AddChildren(var_list_node)

        local in_token = self:peek()
        if not in_token or in_token.value ~= "in" then
            error("Expected 'in' after generic for loop variable list")
        end
        self.token_position = self.token_position + 1 -- consume 'in'

        local expr_list_node = self:parse_expression_list()
        if #expr_list_node.ordered_children == 0 then
            error("Expected expression list after 'in' in generic for loop")
        end
        for_node:AddChildren(expr_list_node)

        local do_token = self:peek()
        if not do_token or do_token.value ~= "do" then error("Expected 'do' after generic for loop expressions") end
        self.token_position = self.token_position + 1 -- consume 'do'

        local body_node = Node:new("block")
        while self:peek() and self:peek().value ~= "end" do
            local statement = self:parse_statement()
            if statement then body_node:AddChildren(statement) end
        end

        local end_token = self:peek()
        if not end_token or end_token.value ~= "end" then error("Expected 'end' to close generic for statement") end
        self.token_position = self.token_position + 1 -- consume 'end'

        for_node:AddChildren(body_node)
        return for_node
    else
        error("Invalid 'for' loop syntax: expected '=' or 'in'")
    end
end

function Parser:parse_while_statement()
    self.token_position = self.token_position + 1 -- consume 'while'
    local while_node = Node:new("while_statement")

    local condition = self:parse_expression()
    if not condition then error("Expected condition after 'while'") end

    local do_token = self:peek()
    if not do_token or do_token.value ~= "do" then error("Expected 'do' after while condition") end
    self.token_position = self.token_position + 1 -- consume 'do'

    local body_node = Node:new("block")
    while self:peek() and self:peek().value ~= "end" do
        local statement = self:parse_statement()
        if statement then body_node:AddChildren(statement) end
    end

    local end_token = self:peek()
    self.token_position = self.token_position + 1 -- consume 'end'

    while_node:AddChildren(condition, body_node)
    
    return while_node
end


function Parser:parse_if_statement()
    self.token_position = self.token_position + 1 -- consume 'if'
    local if_node = Node:new("if_statement")

    local condition = self:parse_expression()
    if not condition then error("Expected condition after 'if'") end

    local then_token = self:peek()
    if not then_token or then_token.value ~= "then" then error("Expected 'then' after if condition") end
    self.token_position = self.token_position + 1 -- consume 'then'

    local body_node = Node:new("block")
    while self:peek() and self:peek().value ~= "end" and self:peek().value ~= "elseif" and self:peek().value ~= "else" do
        local statement = self:parse_statement()
        if statement then body_node:AddChildren(statement) end
    end

    local clause_node = Node:new("if_clause")
    clause_node:AddChildren(condition, body_node)
    if_node:AddChildren(clause_node)

    while self:peek() and self:peek().value == "elseif" do
        self.token_position = self.token_position + 1 -- consume 'elseif'
        local elseif_condition = self:parse_expression()
        if not elseif_condition then error("Expected condition after 'elseif'") end

        local elseif_then_token = self:peek()
        if not elseif_then_token or elseif_then_token.value ~= "then" then error("Expected 'then' after elseif condition") end
        self.token_position = self.token_position + 1 -- consume 'then'

        local elseif_body_node = Node:new("block")
        while self:peek() and self:peek().value ~= "end" and self:peek().value ~= "elseif" and self:peek().value ~= "else" do
            local statement = self:parse_statement()
            if statement then elseif_body_node:AddChildren(statement) end
        end

        local elseif_clause_node = Node:new("elseif_clause")
        elseif_clause_node:AddChildren(elseif_condition, elseif_body_node)
        if_node:AddChildren(elseif_clause_node)
    end

    if self:peek() and self:peek().value == "else" then
        self.token_position = self.token_position + 1 -- consume 'else'
        local else_body_node = Node:new("block")
        while self:peek() and self:peek().value ~= "end" do
            local statement = self:parse_statement()
            if statement then else_body_node:AddChildren(statement) end
        end
        local else_clause_node = Node:new("else_clause")
        else_clause_node:AddChildren(else_body_node)
        if_node:AddChildren(else_clause_node)
    end

    local end_token = self:peek()
    if not end_token or end_token.value ~= "end" then error("Expected 'end' to close if statement") end
    self.token_position = self.token_position + 1 -- consume 'end'

    return if_node
end

function Parser:parse_function_body_content()
    local function_node = Node:new("function_declaration")

    -- ENTER NEW SCOPE: Push current label scope to stack and create fresh one
    table.insert(self.label_scope_stack, self.label_scope)
    self.label_scope = {}

    local open_paren = self:peek()
    if not open_paren or open_paren.value ~= '(' then
        error("Expected '(' after function keyword")
    end
    self.token_position = self.token_position + 1 -- consume '('

    local params_node = Node:new("parameter_list")
    while self:peek() and self:peek().value ~= ')' do
        local param_token = self:peek()
        if param_token and param_token.type == "identifier" then
            self.token_position = self.token_position + 1 -- consume parameter identifier
            params_node:AddChildren(Node:new("identifier", param_token.value, param_token.value))
        elseif param_token and param_token.type == "varargs" then
            self.token_position = self.token_position + 1 -- consume ...
            params_node:AddChildren(Node:new("varargs", "...", "..."))
        else
            error("Expected parameter identifier, '...' or ')'")
        end
        if self:peek() and self:peek().value == ',' then
            self.token_position = self.token_position + 1 -- consume ','
        end
    end

    local close_paren = self:peek()
    if not close_paren or close_paren.value ~= ')' then
        error("Expected ')' after parameter list")
    end
    self.token_position = self.token_position + 1 -- consume ')'
    function_node:AddChildren(params_node)

    -- Parse function body (block of statements)
    local body_node = Node:new("block")
    while self:peek() and not (self:peek().type == "keyword" and self:peek().value == "end") do
        local statement = self:parse_statement()
        if statement then
            body_node:AddChildren(statement)
        else
            -- If a statement can't be parsed, it might be an empty line or comment, just advance
            self.token_position = self.token_position + 1
        end
    end

    local end_token = self:peek()
    if not end_token or not (end_token.type == "keyword" and end_token.value == "end") then
        error("Expected 'end' to close function declaration")
    end
    self.token_position = self.token_position + 1 -- consume 'end'
    function_node:AddChildren(body_node)

    -- EXIT SCOPE: Restore previous label scope
    self.label_scope = table.remove(self.label_scope_stack)

    return function_node
end

function Parser:parse_function_declaration(is_local)
    local name_token = self:peek()
    if name_token and name_token.type == "identifier" then
        local function_node = Node:new("function_declaration")
        function_node.is_local = is_local -- Store if it's a local function

        self.token_position = self.token_position + 1 -- consume identifier
        function_node.identifier = name_token.value

        -- Check for method syntax (e.g., MyClass:new) or table syntax (e.g., MyClass.new)
        if self:peek() and (self:peek().type == "colon" or self:peek().type == "dot") then
            local separator_type = self:peek().type
            self.token_position = self.token_position + 1 -- consume ':' or '.'
            local method_name_token = self:peek()
            if method_name_token and method_name_token.type == "identifier" then
                self.token_position = self.token_position + 1 -- consume method identifier
                function_node.method_name = method_name_token.value -- Store method name separately
                if separator_type == "colon" then
                    function_node.type = "method_declaration" -- Change node type to reflect method
                end
            else
                error("Expected method name after ':' or '.' in function declaration")
            end
        end
        local function_body_node = self:parse_function_body_content()
        function_node:AddChildren(function_body_node.ordered_children[1], function_body_node.ordered_children[2])
        return function_node
    elseif name_token and name_token.value == '(' then
        -- This is an anonymous function, directly parse its body and return the node
        return self:parse_function_body_content()
    else
        error("Expected function name or '(' after 'function'")
    end
end

function Parser:parse()
    self:tokenize()
    self.token_position = 1
    local root = Node:new("Root", nil, nil)
    
    while self.token_position <= #self.tokens do
        local statement_node = self:parse_statement()
        if statement_node then
            if statement_node == root then
                print("ERROR: parse_statement returned the SAME root object!")
                print("  self.token_position = " .. self.token_position)
                print("  Current token: " .. (self.tokens[self.token_position] and self.tokens[self.token_position].type or "nil"))
                os.exit(1)
            end
            if statement_node.type == "Root" then
                print("ERROR: Attempting to add Root node as child!")
                os.exit(1)
            end
            root:AddChildren(statement_node)
        else
            -- If parse_statement returns nil, it means either no statement was found
            -- or we've reached the end of a block (like 'end').
            -- We should only advance if we haven't reached the end of tokens.
            if self.token_position <= #self.tokens then
                self.token_position = self.token_position + 1
            else
                break -- Reached end of tokens
            end
        end
    end

    return root
end


-- Translator function
local function translate(code)
    local parser = Parser:new(code)
    return parser:parse()
end

return {
    Node = Node,
    translate = translate,
    Parser = Parser
}