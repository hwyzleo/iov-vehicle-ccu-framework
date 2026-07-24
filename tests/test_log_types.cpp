#include "log_types.h"
#include <cassert>
#include <iostream>
#include <cstring>

using namespace cgw::fw::log;

void test_log_level_from_string() {
    assert(logLevelFromString("TRACE") == LogLevel::kTrace);
    assert(logLevelFromString("DEBUG") == LogLevel::kDebug);
    assert(logLevelFromString("INFO") == LogLevel::kInfo);
    assert(logLevelFromString("WARN") == LogLevel::kWarn);
    assert(logLevelFromString("ERROR") == LogLevel::kError);
    assert(logLevelFromString("FATAL") == LogLevel::kFatal);
    assert(logLevelFromString("OFF") == LogLevel::kOff);
    assert(logLevelFromString("unknown") == LogLevel::kInfo);  // 默认 INFO
    std::cout << "  [PASS] test_log_level_from_string" << std::endl;
}

void test_log_level_to_string() {
    assert(strcmp(logLevelToString(LogLevel::kTrace), "TRACE") == 0);
    assert(strcmp(logLevelToString(LogLevel::kDebug), "DEBUG") == 0);
    assert(strcmp(logLevelToString(LogLevel::kInfo), "INFO") == 0);
    assert(strcmp(logLevelToString(LogLevel::kWarn), "WARN") == 0);
    assert(strcmp(logLevelToString(LogLevel::kError), "ERROR") == 0);
    assert(strcmp(logLevelToString(LogLevel::kFatal), "FATAL") == 0);
    assert(strcmp(logLevelToString(LogLevel::kOff), "OFF") == 0);
    std::cout << "  [PASS] test_log_level_to_string" << std::endl;
}

void test_field_value_make() {
    auto s = FieldValue::makeString("hello");
    assert(s.type == FieldValueType::kString);
    assert(s.stringVal == "hello");

    auto i = FieldValue::makeInt(42);
    assert(i.type == FieldValueType::kInt64);
    assert(i.intVal == 42);

    auto d = FieldValue::makeDouble(3.14);
    assert(d.type == FieldValueType::kDouble);
    assert(d.doubleVal > 3.13 && d.doubleVal < 3.15);

    auto b = FieldValue::makeBool(true);
    assert(b.type == FieldValueType::kBool);
    assert(b.boolVal == true);
    std::cout << "  [PASS] test_field_value_make" << std::endl;
}

void test_default_config() {
    LogConfig config;
    assert(config.schema_version == 1);
    assert(config.level == LogLevel::kInfo);
    assert(config.strict == false);
    assert(config.async_config.enabled == true);
    assert(config.async_config.queue_size == 4096);
    assert(config.console_config.enabled == true);
    assert(config.file_config.enabled == false);
    assert(config.file_config.root == "/var/log/cgw");
    assert(config.redact_config.identifiers == "mask");
    assert(config.redact_config.raw_payload_max_bytes == 256);
    std::cout << "  [PASS] test_default_config" << std::endl;
}

void test_error_codes() {
    assert(static_cast<uint32_t>(LogError::kConfigInvalid) == 201);
    assert(static_cast<uint32_t>(LogError::kInitFailed) == 202);
    assert(static_cast<uint32_t>(LogError::kSinkFailed) == 203);
    assert(static_cast<uint32_t>(LogError::kQueueOverflow) == 204);
    assert(static_cast<uint32_t>(LogError::kSensitiveViolation) == 205);
    std::cout << "  [PASS] test_error_codes" << std::endl;
}

int main() {
    std::cout << "=== log_types tests ===" << std::endl;
    test_log_level_from_string();
    test_log_level_to_string();
    test_field_value_make();
    test_default_config();
    test_error_codes();
    std::cout << "=== All log_types tests passed ===" << std::endl;
    return 0;
}
