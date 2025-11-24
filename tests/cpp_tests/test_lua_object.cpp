#include "../include/lua_object.hpp"
#include "../include/lua_value.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

// Helper to check if two doubles are close enough
bool doubles_equal(double a, double b) {
    return std::abs(a - b) < 1e-9;
}

void test_basic_table_operations() {
    std::cout << "Testing basic table operations..." << std::endl;
    
    auto table = std::make_shared<LuaObject>();
    
    // Test string keys
    table->set("name", std::string("LuaX"));
    table->set("version", 1.0);
    
    assert(std::holds_alternative<std::string>(table->get("name")));
    assert(std::get<std::string>(table->get("name")) == "LuaX");
    
    assert(std::holds_alternative<double>(table->get("version")));
    assert(doubles_equal(std::get<double>(table->get("version")), 1.0));
    
    // Test integer keys (array-like)
    table->set_item(1LL, std::string("first"));
    table->set_item(2LL, std::string("second"));
    table->set_item(3.0, std::string("third")); // Double as integer
    
    assert(std::get<std::string>(table->get_item(1LL)) == "first");
    assert(std::get<std::string>(table->get_item(2LL)) == "second");
    assert(std::get<std::string>(table->get_item(3.0)) == "third");
    
    std::cout << "✓ Basic table operations passed!" << std::endl;
}

void test_type_conversions() {
    std::cout << "Testing type conversions..." << std::endl;
    
    // Test to_cpp_string
    assert(to_cpp_string(42.0) == "42");
    assert(to_cpp_string(3.14) == "3.14");
    assert(to_cpp_string(42LL) == "42");
    assert(to_cpp_string(std::string("hello")) == "hello");
    assert(to_cpp_string(true) == "true");
    assert(to_cpp_string(false) == "false");
    assert(to_cpp_string(std::monostate{}) == "nil");
    
    // Test get_double
    assert(doubles_equal(get_double(42.0), 42.0));
    assert(doubles_equal(get_double(42LL), 42.0));
    assert(doubles_equal(get_double(std::string("3.14")), 3.14));
    
    // Test get_long_long
    assert(get_long_long(42LL) == 42);
    assert(get_long_long(42.0) == 42);
    assert(get_long_long(std::string("42")) == 42);
    
    std::cout << "✓ Type conversions passed!" << std::endl;
}

void test_truthiness() {
    std::cout << "Testing Lua truthiness..." << std::endl;
    
    // nil and false are falsy
    assert(!is_lua_truthy(std::monostate{}));
    assert(!is_lua_truthy(false));
    
    // Everything else is truthy
    assert(is_lua_truthy(true));
    assert(is_lua_truthy(0LL));
    assert(is_lua_truthy(0.0));
    assert(is_lua_truthy(std::string("")));
    assert(is_lua_truthy(std::string("false")));
    assert(is_lua_truthy(std::make_shared<LuaObject>()));
    
    std::cout << "✓ Truthiness tests passed!" << std::endl;
}

void test_equality() {
    std::cout << "Testing equality comparisons..." << std::endl;
    
    // Same types
    assert(lua_equals(42.0, 42.0));
    assert(lua_equals(42LL, 42LL));
    assert(lua_equals(std::string("hello"), std::string("hello")));
    assert(lua_equals(true, true));
    assert(lua_equals(std::monostate{}, std::monostate{}));
    
    // Mixed numeric types
    assert(lua_equals(42.0, 42LL));
    assert(lua_equals(42LL, 42.0));
    
    // Different values
    assert(!lua_equals(42.0, 43.0));
    assert(!lua_equals(std::string("hello"), std::string("world")));
    assert(!lua_equals(true, false));
    
    // Different types (non-numeric)
    assert(!lua_equals(42.0, std::string("42")));
    assert(!lua_equals(true, 1LL));
    
    // Test reference equality for tables
    auto table1 = std::make_shared<LuaObject>();
    auto table2 = std::make_shared<LuaObject>();
    assert(lua_equals(table1, table1));
    assert(!lua_equals(table1, table2));
    
    // Test inequality
    assert(lua_not_equals(42.0, 43.0));
    assert(!lua_not_equals(42.0, 42.0));
    
    std::cout << "✓ Equality tests passed!" << std::endl;
}

void test_comparisons() {
    std::cout << "Testing comparison operations..." << std::endl;
    
    // Numbers
    assert(lua_less_than(1.0, 2.0));
    assert(lua_less_than(1LL, 2LL));
    assert(lua_less_than(1LL, 2.0));
    assert(lua_less_than(1.0, 2LL));
    assert(!lua_less_than(2.0, 1.0));
    assert(!lua_less_than(2.0, 2.0));
    
    // Strings
    assert(lua_less_than(std::string("a"), std::string("b")));
    assert(!lua_less_than(std::string("b"), std::string("a")));
    
    // Less than or equals
    assert(lua_less_equals(1.0, 2.0));
    assert(lua_less_equals(2.0, 2.0));
    assert(!lua_less_equals(3.0, 2.0));
    
    // Greater than
    assert(lua_greater_than(2.0, 1.0));
    assert(!lua_greater_than(1.0, 2.0));
    
    // Greater than or equals
    assert(lua_greater_equals(2.0, 1.0));
    assert(lua_greater_equals(2.0, 2.0));
    assert(!lua_greater_equals(1.0, 2.0));
    
    std::cout << "✓ Comparison tests passed!" << std::endl;
}

void test_concat() {
    std::cout << "Testing concatenation..." << std::endl;
    
    LuaValue result = lua_concat(std::string("Hello "), std::string("World"));
    assert(std::holds_alternative<std::string>(result));
    assert(std::get<std::string>(result) == "Hello World");
    
    result = lua_concat(std::string("Number: "), 42.0);
    assert(std::get<std::string>(result) == "Number: 42");
    
    result = lua_concat(42LL, std::string(" is the answer"));
    assert(std::get<std::string>(result) == "42 is the answer");
    
    std::cout << "✓ Concatenation tests passed!" << std::endl;
}

void test_rawget_rawset() {
    std::cout << "Testing rawget/rawset..." << std::endl;
    
    auto table = std::make_shared<LuaObject>();
    auto args = std::make_shared<LuaObject>();
    
    // rawset(table, "key", "value")
    args->set_item("1", table);
    args->set_item("2", std::string("key"));
    args->set_item("3", std::string("value"));
    lua_rawset(args);
    
    // rawget(table, "key")
    auto get_args = std::make_shared<LuaObject>();
    get_args->set_item("1", table);
    get_args->set_item("2", std::string("key"));
    auto result = lua_rawget(get_args);
    
    assert(!result.empty());
    assert(std::holds_alternative<std::string>(result[0]));
    assert(std::get<std::string>(result[0]) == "value");
    
    // Test with integer keys
    args->set_item("2", 5LL);
    args->set_item("3", std::string("element"));
    lua_rawset(args);
    
    get_args->set_item("2", 5LL);
    result = lua_rawget(get_args);
    assert(std::get<std::string>(result[0]) == "element");
    
    std::cout << "✓ rawget/rawset tests passed!" << std::endl;
}

void test_rawlen() {
    std::cout << "Testing rawlen..." << std::endl;
    
    auto args = std::make_shared<LuaObject>();
    
    // Test string length
    args->set_item("1", std::string("hello"));
    auto result = lua_rawlen(args);
    assert(std::holds_alternative<double>(result[0]));
    assert(doubles_equal(std::get<double>(result[0]), 5.0));
    
    // Test table length
    auto table = std::make_shared<LuaObject>();
    table->set_item(1LL, std::string("a"));
    table->set_item(2LL, std::string("b"));
    table->set_item(3LL, std::string("c"));
    
    args->set_item("1", table);
    result = lua_rawlen(args);
    assert(doubles_equal(std::get<double>(result[0]), 3.0));
    
    std::cout << "✓ rawlen tests passed!" << std::endl;
}

void test_rawequal() {
    std::cout << "Testing rawequal..." << std::endl;
    
    auto args = std::make_shared<LuaObject>();
    
    // Equal values
    args->set_item("1", 42.0);
    args->set_item("2", 42.0);
    auto result = lua_rawequal(args);
    assert(std::get<bool>(result[0]));
    
    // Different values
    args->set_item("2", 43.0);
    result = lua_rawequal(args);
    assert(!std::get<bool>(result[0]));
    
    // Same table reference
    auto table = std::make_shared<LuaObject>();
    args->set_item("1", table);
    args->set_item("2", table);
    result = lua_rawequal(args);
    assert(std::get<bool>(result[0]));
    
    // Different table references
    auto table2 = std::make_shared<LuaObject>();
    args->set_item("2", table2);
    result = lua_rawequal(args);
    assert(!std::get<bool>(result[0]));
    
    std::cout << "✓ rawequal tests passed!" << std::endl;
}

void test_select() {
    std::cout << "Testing select..." << std::endl;
    
    auto args = std::make_shared<LuaObject>();
    args->set_item("1", std::string("#"));
    args->set_item("2", std::string("a"));
    args->set_item("3", std::string("b"));
    args->set_item("4", std::string("c"));
    
    // select("#", ...)
    auto result = lua_select(args);
    assert(doubles_equal(std::get<double>(result[0]), 3.0));
    
    // select(2, "a", "b", "c")
    args->set_item("1", 2LL);
    result = lua_select(args);
    assert(result.size() == 2);
    assert(std::get<std::string>(result[0]) == "b");
    assert(std::get<std::string>(result[1]) == "c");
    
    std::cout << "✓ select tests passed!" << std::endl;
}

void test_next_pairs() {
    std::cout << "Testing next and pairs..." << std::endl;
    
    auto table = std::make_shared<LuaObject>();
    table->set_item(1LL, std::string("first"));
    table->set_item(2LL, std::string("second"));
    table->set("name", std::string("LuaX"));
    
    auto args = std::make_shared<LuaObject>();
    args->set_item("1", table);
    args->set_item("2", std::monostate{});
    
    // Get first element
    auto result = lua_next(args);
    assert(!result.empty());
    assert(!std::holds_alternative<std::monostate>(result[0]));
    
    // Test pairs returns an iterator
    auto pairs_result = lua_pairs(args);
    assert(pairs_result.size() >= 2);
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(pairs_result[0]));
    
    std::cout << "✓ next/pairs tests passed!" << std::endl;
}

void test_ipairs() {
    std::cout << "Testing ipairs..." << std::endl;
    
    auto table = std::make_shared<LuaObject>();
    table->set_item(1LL, std::string("a"));
    table->set_item(2LL, std::string("b"));
    table->set_item(3LL, std::string("c"));
    
    auto args = std::make_shared<LuaObject>();
    args->set_item("1", table);
    
    auto result = lua_ipairs(args);
    assert(result.size() >= 2);
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(result[0]));
    
    // Test the iterator
    auto iter_args = std::make_shared<LuaObject>();
    iter_args->set_item("1", table);
    iter_args->set_item("2", 0LL);
    
    auto iter_result = ipairs_iterator(iter_args);
    assert(doubles_equal(std::get<double>(iter_result[0]), 1.0));
    assert(std::get<std::string>(iter_result[1]) == "a");
    
    std::cout << "✓ ipairs tests passed!" << std::endl;
}

void test_function_calls() {
    std::cout << "Testing function calls..." << std::endl;
    
    // Create a simple function
    auto func = std::make_shared<LuaFunctionWrapper>([](std::shared_ptr<LuaObject> args) {
        auto a = get_double(args->get_item("1"));
        auto b = get_double(args->get_item("2"));
        return std::vector<LuaValue>{a + b};
    });
    
    auto args = std::make_shared<LuaObject>();
    args->set_item("1", 5.0);
    args->set_item("2", 3.0);
    
    auto result = call_lua_value(func, args);
    assert(doubles_equal(std::get<double>(result[0]), 8.0));
    
    std::cout << "✓ Function call tests passed!" << std::endl;
}

void test_lua_get_length() {
    std::cout << "Testing lua_get_length..." << std::endl;
    
    // String length
    auto str_len = lua_get_length(std::string("hello"));
    assert(doubles_equal(std::get<double>(str_len), 5.0));
    
    // Table length
    auto table = std::make_shared<LuaObject>();
    table->set_item(1LL, std::string("a"));
    table->set_item(2LL, std::string("b"));
    table->set_item(3LL, std::string("c"));
    
    auto table_len = lua_get_length(table);
    assert(doubles_equal(std::get<double>(table_len), 3.0));
    
    std::cout << "✓ lua_get_length tests passed!" << std::endl;
}

void test_lua_get_member() {
    std::cout << "Testing lua_get_member..." << std::endl;
    
    auto table = std::make_shared<LuaObject>();
    table->set("name", std::string("LuaX"));
    table->set_item(1LL, std::string("first"));
    
    auto result = lua_get_member(table, std::string("name"));
    assert(std::get<std::string>(result) == "LuaX");
    
    result = lua_get_member(table, 1LL);
    assert(std::get<std::string>(result) == "first");
    
    std::cout << "✓ lua_get_member tests passed!" << std::endl;
}

void test_assert_function() {
    std::cout << "Testing lua_assert..." << std::endl;
    
    auto args = std::make_shared<LuaObject>();
    
    // Assert with true value
    args->set_item("1", true);
    args->set_item("2", std::string("value"));
    auto result = lua_assert(args);
    assert(result.size() == 2);
    
    // Assert with false value should throw
    args->set_item("1", false);
    args->set_item("2", std::string("error message"));
    bool caught = false;
    try {
        lua_assert(args);
    } catch (const std::runtime_error& e) {
        caught = true;
        assert(std::string(e.what()) == "error message");
    }
    assert(caught);
    
    std::cout << "✓ lua_assert tests passed!" << std::endl;
}

void test_metatable_operations() {
    std::cout << "Testing metatable operations..." << std::endl;
    
    auto table = std::make_shared<LuaObject>();
    auto meta = std::make_shared<LuaObject>();
    
    // Create an __index metamethod
    auto index_table = std::make_shared<LuaObject>();
    index_table->set("default", std::string("fallback"));
    meta->set_item("__index", index_table);
    
    table->set_metatable(meta);
    
    // Try to get a non-existent key
    auto result = table->get_item("default");
    assert(std::holds_alternative<std::string>(result));
    assert(std::get<std::string>(result) == "fallback");
    
    std::cout << "✓ Metatable tests passed!" << std::endl;
}

int main() {
    std::cout << "=== LuaObject Library Test Suite ===" << std::endl << std::endl;
    
    try {
        test_basic_table_operations();
        test_type_conversions();
        test_truthiness();
        test_equality();
        test_comparisons();
        test_concat();
        test_rawget_rawset();
        test_rawlen();
        test_rawequal();
        test_select();
        test_next_pairs();
        test_ipairs();
        test_function_calls();
        test_lua_get_length();
        test_lua_get_member();
        test_assert_function();
        test_metatable_operations();
        
        std::cout << std::endl << "=== ALL TESTS PASSED ✓ ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << std::endl << "TEST FAILED: " << e.what() << std::endl;
        return 1;
    }
}
