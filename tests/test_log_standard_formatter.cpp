#include "log_types.h"
#include "log/log_standard_formatter.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace cgw::fw::log;

void test_basic_format() {
    std::vector<Field> fields = {
        {"schema_version", FieldValue::makeInt(1)},
        {"timestamp", FieldValue::makeString("2026-07-26T03:55:56.848Z")},
        {"level", FieldValue::makeString("INFO")},
        {"service", FieldValue::makeString("cgw")},
        {"module", FieldValue::makeString("main")},
        {"message", FieldValue::makeString("主逻辑执行完成")}
    };

    std::string result = StandardFormatter::format(fields);

    // 验证格式：[时间] [服务] [级别] 消息
    assert(result.find("[2026-07-26") != std::string::npos);
    assert(result.find("[cgw]") != std::string::npos);
    assert(result.find("[info]") != std::string::npos);
    assert(result.find("主逻辑执行完成") != std::string::npos);

    std::cout << "  [PASS] test_basic_format" << std::endl;
    std::cout << "    Output: " << result << std::endl;
}

void test_with_extra_fields() {
    std::vector<Field> fields = {
        {"timestamp", FieldValue::makeString("2026-07-26T03:55:56.848Z")},
        {"level", FieldValue::makeString("ERROR")},
        {"service", FieldValue::makeString("cgw")},
        {"message", FieldValue::makeString("连接失败")},
        {"host", FieldValue::makeString("192.168.1.1")},
        {"port", FieldValue::makeInt(8080)}
    };

    std::string result = StandardFormatter::format(fields);

    // 验证附加字段
    assert(result.find("host=192.168.1.1") != std::string::npos);
    assert(result.find("port=8080") != std::string::npos);

    std::cout << "  [PASS] test_with_extra_fields" << std::endl;
    std::cout << "    Output: " << result << std::endl;
}

void test_level_is_lowercase() {
    std::vector<Field> fields = {
        {"timestamp", FieldValue::makeString("2026-07-26T03:55:56.848Z")},
        {"level", FieldValue::makeString("WARN")},
        {"service", FieldValue::makeString("cgw")},
        {"message", FieldValue::makeString("警告")}
    };

    std::string result = StandardFormatter::format(fields);

    assert(result.find("[warn]") != std::string::npos);
    assert(result.find("[WARN]") == std::string::npos);

    std::cout << "  [PASS] test_level_is_lowercase" << std::endl;
    std::cout << "    Output: " << result << std::endl;
}

void test_empty_fields() {
    std::vector<Field> fields;
    std::string result = StandardFormatter::format(fields);

    // 不应崩溃，返回基本格式
    assert(!result.empty());

    std::cout << "  [PASS] test_empty_fields" << std::endl;
    std::cout << "    Output: " << result << std::endl;
}

void test_missing_timestamp() {
    std::vector<Field> fields = {
        {"level", FieldValue::makeString("INFO")},
        {"service", FieldValue::makeString("cgw")},
        {"message", FieldValue::makeString("test")}
    };

    std::string result = StandardFormatter::format(fields);

    // 应使用默认时间占位符
    assert(result.find("[----") != std::string::npos);

    std::cout << "  [PASS] test_missing_timestamp" << std::endl;
    std::cout << "    Output: " << result << std::endl;
}

int main() {
    std::cout << "Running StandardFormatter tests..." << std::endl;

    test_basic_format();
    test_with_extra_fields();
    test_level_is_lowercase();
    test_empty_fields();
    test_missing_timestamp();

    std::cout << "All StandardFormatter tests PASSED!" << std::endl;
    return 0;
}
