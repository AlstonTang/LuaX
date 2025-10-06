--[[
    Lua Script: Simple Inventory & Crafting System
    This script demonstrates the use of tables, functions, loops,
    and conditional logic to manage an inventory and craft new items.
]]--

-- Global tables for the system
local inventory = {}
local recipes = {}
local item_data = {}

-- --- Item Data Setup ---
-- Defines all possible items with their display name and maximum stack size
function setup_item_data()
    item_data = {
        -- Materials
        ["WOOD"]    = {name = "Wood Log", stack_max = 20},
        ["STONE"]   = {name = "Rough Stone", stack_max = 30},
        ["IRON_ORE"]= {name = "Iron Ore", stack_max = 10},
        -- Crafted Items
        ["PLANKS"]  = {name = "Wooden Planks", stack_max = 50},
        ["STICK"]   = {name = "Stick", stack_max = 99},
        ["AXE"]     = {name = "Stone Axe", stack_max = 1},
        ["PICKAXE"] = {name = "Stone Pickaxe", stack_max = 1},
        ["FURNACE"] = {name = "Crude Furnace", stack_max = 1},
        ["IRON_INGOT"] = {name = "Iron Ingot", stack_max = 10}
    }
end

-- --- Recipe Data Setup ---
-- Defines crafting recipes: {result = count, ingredients = {item_key = count, ...}}
function setup_recipes()
    recipes = {
        -- Planks from Wood
        ["PLANKS"] = {
            result_count = 4,
            ingredients = {["WOOD"] = 1}
        },
        -- Stick from Planks
        ["STICK"] = {
            result_count = 4,
            ingredients = {["PLANKS"] = 1}
        },
        -- Stone Axe
        ["AXE"] = {
            result_count = 1,
            ingredients = {
                ["WOOD"] = 1,
                ["STONE"] = 3,
                ["STICK"] = 2
            }
        },
        -- Crude Furnace
        ["FURNACE"] = {
            result_count = 1,
            ingredients = {
                ["STONE"] = 8
            }
        },
        -- Smelting (Using 'FURNACE' as a crafting bench for complexity)
        -- Note: For simplicity, this acts like a standard craft, not a timed smelt.
        ["IRON_INGOT"] = {
            result_count = 1,
            ingredients = {
                ["IRON_ORE"] = 1,
                ["FURNACE"] = 1 -- Requires a furnace in inventory to 'craft'
            }
        }
    }
end

-- --- Inventory Management Functions ---

---
-- Adds an item to the inventory, handling stack limits.
-- @param item_key The key of the item (e.g., "WOOD")
-- @param count The number of items to add
---
function add_item(item_key, count)
    local item_data_ref = item_data[item_key]
    if not item_data_ref then
        print("Error: Item key '" .. item_key .. "' not found!")
        return
    end

    local item_name = item_data_ref.name
    local stack_max = item_data_ref.stack_max
    local items_left_to_add = count
    local initial_count = inventory[item_key] or 0
    local total_added = 0

    -- Add to existing stack(s) or create new ones
    while items_left_to_add > 0 do
        local current_stack_count = inventory[item_key] or 0
        local space_in_stack = stack_max - current_stack_count

        if space_in_stack > 0 then
            local add_amount = math.min(items_left_to_add, space_in_stack)
            inventory[item_key] = current_stack_count + add_amount
            items_left_to_add = items_left_to_add - add_amount
            total_added = total_added + add_amount
        else
            -- For simplicity in this script, we'll only use a single key-value
            -- pair for an item, meaning it stops when the single stack is full.
            -- A true complex inventory would use a table of slots.
            print("Notice: Inventory full for " .. item_name .. ". Cannot add " .. items_left_to_add .. " more.")
            break -- Break the loop as the single stack is full
        end
    end

    if total_added > 0 then
        print("-> Added " .. total_added .. "x " .. item_name .. ".")
    end
end

---
-- Removes an item from the inventory.
-- @param item_key The key of the item (e.g., "WOOD")
-- @param count The number of items to remove
-- @return boolean True if removal was successful, false otherwise.
---
function remove_item(item_key, count)
    local current_count = inventory[item_key] or 0

    if current_count < count then
        print("! ERROR: Insufficient " .. (item_data[item_key] and item_data[item_key].name or item_key) .. ".")
        return false
    else
        inventory[item_key] = current_count - count
        if inventory[item_key] == 0 then
            inventory[item_key] = nil -- Clean up empty entry
        end
        return true
    end
end

---
-- Checks if the inventory contains the required ingredients for a recipe.
-- @param recipe_key The key of the recipe (e.g., "AXE")
-- @return boolean True if all ingredients are available, false otherwise.
---
function check_ingredients(recipe_key)
    local recipe = recipes[recipe_key]
    if not recipe then
        print("! ERROR: Recipe '" .. recipe_key .. "' not found.")
        return false
    end

    for item_key, required_count in pairs(recipe.ingredients) do
        local current_count = inventory[item_key] or 0
        if current_count < required_count then
            print("! MISSING: Need " .. required_count .. "x " .. item_data[item_key].name .. ", have " .. current_count .. ".")
            return false
        end
    end
    return true
end

---
-- Attempts to craft an item using a recipe.
-- @param recipe_key The key of the recipe (e.g., "AXE")
---
function craft(recipe_key)
    print("\n--- Attempting to Craft: " .. (item_data[recipe_key] and item_data[recipe_key].name or recipe_key) .. " ---")
    if not check_ingredients(recipe_key) then
        print("! CRAFT FAILED: Not enough ingredients.")
        return
    end

    local recipe = recipes[recipe_key]
    local result_name = item_data[recipe_key].name

    -- 1. Deduct ingredients
    for item_key, required_count in pairs(recipe.ingredients) do
        remove_item(item_key, required_count)
    end
    print("- Used ingredients.")

    -- 2. Add crafted result
    add_item(recipe_key, recipe.result_count)
    print("-> Successfully crafted " .. recipe.result_count .. "x " .. result_name .. "!")
end

-- --- Display Functions ---

function display_inventory()
    print("\n--- Current Inventory ---")
    local is_empty = true
    for item_key, count in pairs(inventory) do
        local name = item_data[item_key].name
        print("- " .. name .. ": " .. count)
        is_empty = false
    end
    if is_empty then
        print("(Inventory is empty)")
    end
end

function display_recipes()
    print("\n--- Available Recipes ---")
    for result_key, recipe in pairs(recipes) do
        local result_name = item_data[result_key].name
        io.write("- " .. result_name .. " (" .. recipe.result_count .. "x): Requires ")
        local ingredients_list = {}
        for item_key, count in pairs(recipe.ingredients) do
            table.insert(ingredients_list, count .. "x " .. item_data[item_key].name)
        end
        print(table.concat(ingredients_list, ", "))
    end
end

-- --- Script Execution ---

-- Initialize data
setup_item_data()
setup_recipes()

print("--- System Initialized ---")

-- 1. Initial gathering/discovery
print("\n--- Initial Gathering Simulation ---")
add_item("WOOD", 5)
add_item("STONE", 10)
add_item("IRON_ORE", 2)
add_item("IRON_ORE", 9) -- Demonstrating stack limit notice (10 max)

display_inventory()
display_recipes()

-- 2. First crafting attempts
print("\n--- Crafting Phase 1: Basic Materials ---")
craft("PLANKS") -- Makes 4 Planks, uses 1 Wood
craft("PLANKS") -- Makes 4 Planks, uses 1 Wood
craft("STICK")  -- Makes 4 Sticks, uses 1 Plank

display_inventory()

-- 3. Second crafting attempt (Tool)
print("\n--- Crafting Phase 2: Tool ---")
craft("AXE") -- Requires 1 Wood, 3 Stone, 2 Stick

display_inventory()

-- 4. Third crafting attempt (Complex item and 'smelting')
print("\n--- Crafting Phase 3: Furnace and Smelting ---")
craft("FURNACE") -- Requires 8 Stone

-- Try to 'smelt' an ingot, which requires the furnace in inventory
craft("IRON_INGOT") -- Requires 1 Iron Ore, 1 Furnace

display_inventory()

-- 5. Failed attempt due to missing materials
print("\n--- Failed Craft Attempt ---")
craft("AXE") -- Will fail due to missing Wood/Stone/Stick

-- Final state check
print("\n--- Final Check ---")
display_inventory()