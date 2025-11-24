#include "../include/lua_object.hpp"
#include "../include/string.hpp"
#include "../include/table.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

// Forward declarations for functions not in headers
std::vector<LuaValue> string_byte(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> string_char(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> string_find(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> string_format(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> string_gsub(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> string_len(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> string_lower(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> string_upper(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> string_rep(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> string_reverse(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> string_sub(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> table_concat(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> table_insert(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> table_remove(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> table_sort(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> table_unpack(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> table_pack(std::shared_ptr<LuaObject> args);
std::vector<LuaValue> table_move(std::shared_ptr<LuaObject> args);


bool doubles_equal(double a, double b) {
    return std::abs(a - b) < 1e-9;
}

// ========================================
// STRING LIBRARY TESTS
// ========================================

void test_string_byte() {
    std::cout << "Testing string.byte..." << std::endl;
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", std::string("ABC"));
    
    auto result = string_byte(args);
    assert(result.size() == 1);
    assert(doubles_equal(std::get<double>(result[0]), 65.0)); // 'A'
    
    // Test range
    args->set("2", 1.0);
    args->set("3", 3.0);
    result = string_byte(args);
    assert(result.size() == 3);
    assert(doubles_equal(std::get<double>(result[0]), 65.0)); // 'A'
    assert(doubles_equal(std::get<double>(result[1]), 66.0)); // 'B'
    assert(doubles_equal(std::get<double>(result[2]), 67.0)); // 'C'
    
    std::cout << "✓ string.byte passed!" << std::endl;
}

void test_string_char() {
    std::cout << "Testing string.char..." << std::endl;
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", 72.0);  // 'H'
    args->set("2", 105.0); // 'i'
    
    auto result = string_char(args);
    assert(std::get<std::string>(result[0]) == "Hi");
    
    std::cout << "✓ string.char passed!" << std::endl;
}

void test_string_find() {
    std::cout << "Testing string.find..." << std::endl;
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", std::string("Hello World"));
    args->set("2", std::string("World"));
    args->set("4", true); // plain search
    
    auto result = string_find(args);
    assert(doubles_equal(std::get<double>(result[0]), 7.0));
    assert(doubles_equal(std::get<double>(result[1]), 11.0));
    
    // Test not found
    args->set("2", std::string("xyz"));
    result = string_find(args);
    assert(std::holds_alternative<std::monostate>(result[0]));
    
    std::cout << "✓ string.find passed!" << std::endl;
}

void test_string_format() {
    std::cout << "Testing string.format..." << std::endl;
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", std::string("Hello %s, you are %d years old"));
    args->set("2", std::string("Alice"));
    args->set("3", 25.0);
    
    auto result = string_format(args);
    assert(std::get<std::string>(result[0]) == "Hello Alice, you are 25 years old");
    
    std::cout << "✓ string.format passed!" << std::endl;
}

void test_string_gsub() {
    std::cout << "Testing string.gsub..." << std::endl;
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", std::string("hello world"));
    args->set("2", std::string("world"));
    args->set("3", std::string("LuaX"));
    
    auto result = string_gsub(args);
    assert(std::get<std::string>(result[0]) == "hello LuaX");
    assert(doubles_equal(std::get<double>(result[1]), 1.0)); // 1 substitution
    
    std::cout << "✓ string.gsub passed!" << std::endl;
}

void test_string_len() {
    std::cout << "Testing string.len..." << std::endl;
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", std::string("hello"));
    
    auto result = string_len(args);
    assert(doubles_equal(std::get<double>(result[0]), 5.0));
    
    std::cout << "✓ string.len passed!" << std::endl;
}

void test_string_lower_upper() {
    std::cout << "Testing string.lower/upper..." << std::endl;
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", std::string("Hello"));
    
    auto result = string_lower(args);
    assert(std::get<std::string>(result[0]) == "hello");
    
    result = string_upper(args);
    assert(std::get<std::string>(result[0]) == "HELLO");
    
    std::cout << "✓ string.lower/upper passed!" << std::endl;
}

void test_string_rep() {
    std::cout << "Testing string.rep..." << std::endl;
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", std::string("ab"));
    args->set("2", 3.0);
    
    auto result = string_rep(args);
    assert(std::get<std::string>(result[0]) == "ababab");
    
    // Test with separator
    args->set("3", std::string("-"));
    result = string_rep(args);
    assert(std::get<std::string>(result[0]) == "ab-ab-ab");
    
    std::cout << "✓ string.rep passed!" << std::endl;
}

void test_string_reverse() {
    std::cout << "Testing string.reverse..." << std::endl;
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", std::string("hello"));
    
    auto result = string_reverse(args);
    assert(std::get<std::string>(result[0]) == "olleh");
    
    std::cout << "✓ string.reverse passed!" << std::endl;
}

void test_string_sub() {
    std::cout << "Testing string.sub..." << std::endl;
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", std::string("Hello World"));
    args->set("2", 1.0);
    args->set("3", 5.0);
    
    auto result = string_sub(args);
    assert(std::get<std::string>(result[0]) == "Hello");
    
    // Test negative indices
    args->set("2", -5.0);
    args->set("3", -1.0);
    result = string_sub(args);
    assert(std::get<std::string>(result[0]) == "World");
    
    std::cout << "✓ string.sub passed!" << std::endl;
}

void test_string_library_creation() {
    std::cout << "Testing string library creation..." << std::endl;
    
    auto string_lib = create_string_library();
    
    // Verify all functions are present
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(string_lib->get("byte")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(string_lib->get("char")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(string_lib->get("find")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(string_lib->get("format")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(string_lib->get("gsub")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(string_lib->get("len")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(string_lib->get("lower")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(string_lib->get("upper")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(string_lib->get("rep")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(string_lib->get("reverse")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(string_lib->get("sub")));
    
    std::cout << "✓ String library creation passed!" << std::endl;
}

void test_string_metatable_access() {
    std::cout << "Testing string metatable access..." << std::endl;
    
    // This tests the lua_get_member functionality
    // When accessing methods on strings like str:upper(), it should go through the string metatable
    
    // Set up global string library
    _G->set("string", create_string_library());
    
    // Test that we can get methods through lua_get_member
    // This simulates str:upper() access pattern
    auto str = std::string("hello");
    auto upper_method = lua_get_member(str, std::string("upper"));
    
    // The method should be callable
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(upper_method));
    
    std::cout << "✓ String metatable access passed!" << std::endl;
}

// ========================================
// TABLE LIBRARY TESTS
// ========================================

void test_table_concat() {
    std::cout << "Testing table.concat..." << std::endl;
    
    auto table = std::make_shared<LuaObject>();
    table->set_item(1LL, std::string("a"));
    table->set_item(2LL, std::string("b"));
    table->set_item(3LL, std::string("c"));
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", table);
    
    auto result = table_concat(args);
    assert(std::get<std::string>(result[0]) == "abc");
    
    // With separator
    args->set("2", std::string("-"));
    result = table_concat(args);
    assert(std::get<std::string>(result[0]) == "a-b-c");
    
    std::cout << "✓ table.concat passed!" << std::endl;
}

void test_table_insert() {
    std::cout << "Testing table.insert..." << std::endl;
    
    auto table = std::make_shared<LuaObject>();
    table->set_item(1LL, std::string("a"));
    table->set_item(2LL, std::string("b"));
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", table);
    args->set("2", std::string("c"));
    
    table_insert(args);
    
    // Should have inserted at end
    assert(std::get<std::string>(table->get_item(3LL)) == "c");
    
    // Test insert at position
    args->set("2", 2.0);
    args->set("3", std::string("x"));
    table_insert(args);
    
    assert(std::get<std::string>(table->get_item(2LL)) == "x");
    assert(std::get<std::string>(table->get_item(3LL)) == "b");
    assert(std::get<std::string>(table->get_item(4LL)) == "c");
    
    std::cout << "✓ table.insert passed!" << std::endl;
}

void test_table_remove() {
    std::cout << "Testing table.remove..." << std::endl;
    
    auto table = std::make_shared<LuaObject>();
    table->set_item(1LL, std::string("a"));
    table->set_item(2LL, std::string("b"));
    table->set_item(3LL, std::string("c"));
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", table);
    
    auto result = table_remove(args);
    
    // Should remove last element
    assert(std::get<std::string>(result[0]) == "c");
    assert(std::holds_alternative<std::monostate>(table->get_item(3LL)));
    
    // Remove from position
    args->set("2", 1.0);
    result = table_remove(args);
    assert(std::get<std::string>(result[0]) == "a");
    assert(std::get<std::string>(table->get_item(1LL)) == "b");
    
    std::cout << "✓ table.remove passed!" << std::endl;
}

void test_table_sort() {
    std::cout << "Testing table.sort..." << std::endl;
    
    auto table = std::make_shared<LuaObject>();
    table->set_item(1LL, 3.0);
    table->set_item(2LL, 1.0);
    table->set_item(3LL, 4.0);
    table->set_item(4LL, 2.0);
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", table);
    
    table_sort(args);
    
    assert(doubles_equal(std::get<double>(table->get_item(1LL)), 1.0));
    assert(doubles_equal(std::get<double>(table->get_item(2LL)), 2.0));
    assert(doubles_equal(std::get<double>(table->get_item(3LL)), 3.0));
    assert(doubles_equal(std::get<double>(table->get_item(4LL)), 4.0));
    
    std::cout << "✓ table.sort passed!" << std::endl;
}

void test_table_sort_with_comparator() {
    std::cout << "Testing table.sort with custom comparator..." << std::endl;
    
    auto table = std::make_shared<LuaObject>();
    table->set_item(1LL, 1.0);
    table->set_item(2LL, 2.0);
    table->set_item(3LL, 3.0);
    
    // Custom comparator for descending order
    auto comp = std::make_shared<LuaFunctionWrapper>([](std::shared_ptr<LuaObject> args) {
        double a = get_double(args->get("1"));
        double b = get_double(args->get("2"));
        return std::vector<LuaValue>{a > b}; // descending
    });
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", table);
    args->set("2", comp);
    
    table_sort(args);
    
    assert(doubles_equal(std::get<double>(table->get_item(1LL)), 3.0));
    assert(doubles_equal(std::get<double>(table->get_item(2LL)), 2.0));
    assert(doubles_equal(std::get<double>(table->get_item(3LL)), 1.0));
    
    std::cout << "✓ table.sort with comparator passed!" << std::endl;
}

void test_table_unpack() {
    std::cout << "Testing table.unpack..." << std::endl;
    
    auto table = std::make_shared<LuaObject>();
    table->set_item(1LL, std::string("a"));
    table->set_item(2LL, std::string("b"));
    table->set_item(3LL, std::string("c"));
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", table);
    
    auto result = table_unpack(args);
    assert(result.size() == 3);
    assert(std::get<std::string>(result[0]) == "a");
    assert(std::get<std::string>(result[1]) == "b");
    assert(std::get<std::string>(result[2]) == "c");
    
    // Test with range
    args->set("2", 2.0);
    args->set("3", 3.0);
    result = table_unpack(args);
    assert(result.size() == 2);
    assert(std::get<std::string>(result[0]) == "b");
    assert(std::get<std::string>(result[1]) == "c");
    
    std::cout << "✓ table.unpack passed!" << std::endl;
}

void test_table_pack() {
    std::cout << "Testing table.pack..." << std::endl;
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", std::string("a"));
    args->set("2", std::string("b"));
    args->set("3", std::string("c"));
    
    auto result = table_pack(args);
    auto packed = std::get<std::shared_ptr<LuaObject>>(result[0]);
    
    assert(std::get<std::string>(packed->get_item(1LL)) == "a");
    assert(std::get<std::string>(packed->get_item(2LL)) == "b");
    assert(std::get<std::string>(packed->get_item(3LL)) == "c");
    assert(doubles_equal(std::get<double>(packed->get("n")), 3.0));
    
    std::cout << "✓ table.pack passed!" << std::endl;
}

void test_table_move() {
    std::cout << "Testing table.move..." << std::endl;
    
    auto table1 = std::make_shared<LuaObject>();
    table1->set_item(1LL, std::string("a"));
    table1->set_item(2LL, std::string("b"));
    table1->set_item(3LL, std::string("c"));
    
    auto table2 = std::make_shared<LuaObject>();
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", table1);  // source
    args->set("2", 1.0);     // from
    args->set("3", 2.0);     // to
    args->set("4", 1.0);     // destination index
    args->set("5", table2);  // destination table
    
    table_move(args);
    
    assert(std::get<std::string>(table2->get_item(1LL)) == "a");
    assert(std::get<std::string>(table2->get_item(2LL)) == "b");
    
    std::cout << "✓ table.move passed!" << std::endl;
}

void test_table_library_creation() {
    std::cout << "Testing table library creation..." << std::endl;
    
    auto table_lib = create_table_library();
    
    // Verify all functions are present
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(table_lib->get("concat")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(table_lib->get("insert")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(table_lib->get("move")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(table_lib->get("pack")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(table_lib->get("remove")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(table_lib->get("sort")));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(table_lib->get("unpack")));
    
    std::cout << "✓ Table library creation passed!" << std::endl;
}

// ========================================
// METATABLE-SPECIFIC TESTS
// ========================================

void test_string_metatable_functionality() {
    std::cout << "Testing string metatable functionality..." << std::endl;
    
    // Set up string library in global environment
    _G->set("string", create_string_library());
    
    // This tests that string values can access string library methods
    // through the metatable mechanism (via lua_get_member)
    
    std::string test_str = "hello";
    
    // Test accessing various string methods
    auto len_method = lua_get_member(test_str, std::string("len"));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(len_method));
    
    auto upper_method = lua_get_member(test_str, std::string("upper"));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(upper_method));
    
    auto sub_method = lua_get_member(test_str, std::string("sub"));
    assert(std::holds_alternative<std::shared_ptr<LuaFunctionWrapper>>(sub_method));
    
    std::cout << "✓ String metatable functionality passed!" << std::endl;
}

void test_table_with_custom_metatable() {
    std::cout << "Testing table with custom metatable..." << std::endl;
    
    auto table = std::make_shared<LuaObject>();
    auto metatable = std::make_shared<LuaObject>();
    
    // Create custom __len metamethod
    auto len_func = std::make_shared<LuaFunctionWrapper>([](std::shared_ptr<LuaObject> args) {
        return std::vector<LuaValue>{100.0}; // Always return 100
    });
    
    metatable->set_item("__len", len_func);
    table->set_metatable(metatable);
    
    // Test that lua_get_length calls the metamethod
    auto length = lua_get_length(table);
    assert(doubles_equal(std::get<double>(length), 100.0));
    
    std::cout << "✓ Table with custom metatable passed!" << std::endl;
}

void test_table_metamethod_index() {
    std::cout << "Testing table __index metamethod..." << std::endl;
    
    auto table = std::make_shared<LuaObject>();
    auto metatable = std::make_shared<LuaObject>();
    auto fallback = std::make_shared<LuaObject>();
    
    // Set up fallback table
    fallback->set("default_value", std::string("fallback"));
    
    // Set __index to point to fallback table
    metatable->set_item("__index", fallback);
    table->set_metatable(metatable);
    
    // Access a key that doesn't exist in table
    auto result = table->get_item("default_value");
    assert(std::holds_alternative<std::string>(result));
    assert(std::get<std::string>(result) == "fallback");
    
    std::cout << "✓ Table __index metamethod passed!" << std::endl;
}

void test_table_metamethod_newindex() {
    std::cout << "Testing table __newindex metamethod..." << std::endl;
    
    auto table = std::make_shared<LuaObject>();
    auto metatable = std::make_shared<LuaObject>();
    auto storage = std::make_shared<LuaObject>();
    
    // Set __newindex to redirect writes to storage table
    metatable->set_item("__newindex", storage);
    table->set_metatable(metatable);
    
    // Try to set a new key
    table->set_item("new_key", std::string("value"));
    
    // It should be in the storage table, not the original table
    auto result = storage->get_item("new_key");
    assert(std::holds_alternative<std::string>(result));
    assert(std::get<std::string>(result) == "value");
    
    std::cout << "✓ Table __newindex metamethod passed!" << std::endl;
}

void test_table_sort_with_metamethods() {
    std::cout << "Testing table.sort with objects having __lt metamethod..." << std::endl;
    
    // Create custom objects with comparison metamethod
    auto obj1 = std::make_shared<LuaObject>();
    obj1->set("value", 3.0);
    
    auto obj2 = std::make_shared<LuaObject>();
    obj2->set("value", 1.0);
    
    auto obj3 = std::make_shared<LuaObject>();
    obj3->set("value", 2.0);
    
    // Create a shared metatable with __lt
    auto meta = std::make_shared<LuaObject>();
    auto lt_func = std::make_shared<LuaFunctionWrapper>([](std::shared_ptr<LuaObject> args) {
        auto a = get_object(args->get("1"));
        auto b = get_object(args->get("2"));
        double a_val = get_double(a->get("value"));
        double b_val = get_double(b->get("value"));
        return std::vector<LuaValue>{a_val < b_val};
    });
    meta->set_item("__lt", lt_func);
    
    obj1->set_metatable(meta);
    obj2->set_metatable(meta);
    obj3->set_metatable(meta);
    
    // Create table and sort
    auto table = std::make_shared<LuaObject>();
    table->set_item(1LL, obj1);
    table->set_item(2LL, obj2);
    table->set_item(3LL, obj3);
    
    auto args = std::make_shared<LuaObject>();
    args->set("1", table);
    
    table_sort(args);
    
    // Verify sorting used the metamethod
    auto first = get_object(table->get_item(1LL));
    auto second = get_object(table->get_item(2LL));
    auto third = get_object(table->get_item(3LL));
    
    assert(doubles_equal(get_double(first->get("value")), 1.0));
    assert(doubles_equal(get_double(second->get("value")), 2.0));
    assert(doubles_equal(get_double(third->get("value")), 3.0));
    
    std::cout << "✓ table.sort with metamethods passed!" << std::endl;
}

int main() {
    std::cout << "=== String and Table Library Test Suite ===" << std::endl << std::endl;
    
    try {
        // String library tests
        test_string_byte();
        test_string_char();
        test_string_find();
        test_string_format();
        test_string_gsub();
        test_string_len();
        test_string_lower_upper();
        test_string_rep();
        test_string_reverse();
        test_string_sub();
        test_string_library_creation();
        test_string_metatable_access();
        
        // Table library tests
        test_table_concat();
        test_table_insert();
        test_table_remove();
        test_table_sort();
        test_table_sort_with_comparator();
        test_table_unpack();
        test_table_pack();
        test_table_move();
        test_table_library_creation();
        
        // Metatable-specific tests
        test_string_metatable_functionality();
        test_table_with_custom_metatable();
        test_table_metamethod_index();
        test_table_metamethod_newindex();
        test_table_sort_with_metamethods();
        
        std::cout << std::endl << "=== ALL TESTS PASSED ✓ ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << std::endl << "TEST FAILED: " << e.what() << std::endl;
        return 1;
    }
}
