#include "log_types.h"
#include "log/log_json_formatter.h"
#include <cassert>
#include <iostream>

using namespace cgw::fw::log;

void test_json_basic_format() {
    std::vector<Field> fields;
    fields.push_back({"level", FieldValue::makeString("INFO")});
    fields.push_back({"count", FieldValue::makeInt(42)});

    std::string json = JsonLineFormatter::format(fields);

    assert(json.front() == '{');
    assert(json.back() == '}');
    assert(json.find("\"level\":\"INFO\"") != std::string::npos);
    assert(json.find("\"count\":42") != std::string::npos);

    std::cout << "  [PASS] test_json_basic_format" << std::endl;
}

void test_json_escape_special_chars() {
    std::vector<Field> fields;
    fields.push_back({"message", FieldValue::makeString("Hello\nWorld\t\"Quoted\"")});

    std::string json = JsonLineFormatter::format(fields);
    assert(json.find("\\n") != std::string::npos);
    assert(json.find("\\t") != std::string::npos);
    assert(json.find("\\\"") != std::string::npos);

    std::cout << "  [PASS] test_json_escape_special_chars" << std::endl;
}

void test_json_field_order_preserved() {
    std::vector<Field> fields;
    fields.push_back({"alpha", FieldValue::makeString("a")});
    fields.push_back({"beta", FieldValue::makeString("b")});
    fields.push_back({"gamma", FieldValue::makeString("c")});

    std::string json = JsonLineFormatter::format(fields);

    size_t posAlpha = json.find("\"alpha\"");
    size_t posBeta = json.find("\"beta\"");
    size_t posGamma = json.find("\"gamma\"");

    assert(posAlpha < posBeta);
    assert(posBeta < posGamma);

    std::cout << "  [PASS] test_json_field_order_preserved" << std::endl;
}

void test_json_value_types() {
    std::vector<Field> fields;
    fields.push_back({"str", FieldValue::makeString("hello")});
    fields.push_back({"num", FieldValue::makeInt(123)});
    fields.push_back({"dbl", FieldValue::makeDouble(3.14)});
    fields.push_back({"flag", FieldValue::makeBool(true)});

    std::string json = JsonLineFormatter::format(fields);

    assert(json.find("\"str\":\"hello\"") != std::string::npos);
    assert(json.find("\"num\":123") != std::string::npos);
    assert(json.find("\"flag\":true") != std::string::npos);

    std::cout << "  [PASS] test_json_value_types" << std::endl;
}

void test_json_single_line_no_newline() {
    std::vector<Field> fields;
    fields.push_back({"key", FieldValue::makeString("value")});

    std::string json = JsonLineFormatter::format(fields);

    assert(json.find('\n') == std::string::npos);
    assert(json.find('\r') == std::string::npos);

    std::cout << "  [PASS] test_json_single_line_no_newline" << std::endl;
}

int main() {
    std::cout << "Running JsonLineFormatter tests..." << std::endl;
    test_json_basic_format();
    test_json_escape_special_chars();
    test_json_field_order_preserved();
    test_json_value_types();
    test_json_single_line_no_newline();
    std::cout << "All JsonLineFormatter tests passed!" << std::endl;
    return 0;
}
