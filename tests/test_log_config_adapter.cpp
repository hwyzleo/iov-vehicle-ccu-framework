#include "log_types.h"
#include "log/log_config_adapter.h"
#include <cassert>
#include <iostream>

using namespace cgw::fw::log;

void test_default_config() {
    LogConfig config = LogConfigAdapter::getDefaultConfig();
    assert(config.schema_version == 1);
    assert(config.level == LogLevel::kInfo);
    assert(config.strict == false);
    assert(config.console_config.enabled == true);
    assert(config.file_config.enabled == false);
    std::cout << "  [PASS] test_default_config" << std::endl;
}

void test_valid_config() {
    std::string yaml = R"(
common:
  log:
    schema_version: 1
    level: WARN
    strict: false
    async:
      enabled: true
      queue_size: 8192
      flush_interval_ms: 500
    console:
      enabled: true
    file:
      enabled: false
    redact:
      identifiers: mask
      raw_payload_max_bytes: 512
)";
    auto result = LogConfigAdapter::loadFromYamlString(yaml);
    assert(result.second.code == LogError::kOk);
    assert(result.first.level == LogLevel::kWarn);
    assert(result.first.async_config.queue_size == 8192);
    assert(result.first.redact_config.raw_payload_max_bytes == 512);
    std::cout << "  [PASS] test_valid_config" << std::endl;
}

void test_invalid_schema_version() {
    std::string yaml = R"(
common:
  log:
    schema_version: 2
    level: INFO
)";
    auto result = LogConfigAdapter::loadFromYamlString(yaml);
    assert(result.second.code == LogError::kConfigInvalid);
    assert(result.second.message.find("schema_version") != std::string::npos);
    std::cout << "  [PASS] test_invalid_schema_version" << std::endl;
}

void test_file_budget_violation() {
    std::string yaml = R"(
common:
  log:
    schema_version: 1
    level: INFO
    file:
      enabled: true
      root: /tmp/tbox_test
      max_file_size_mb: 30
      max_files: 5
      total_budget_mb: 100
)";
    auto result = LogConfigAdapter::loadFromYamlString(yaml);
    assert(result.second.code == LogError::kConfigInvalid);
    assert(result.second.message.find("total_budget_mb") != std::string::npos);
    std::cout << "  [PASS] test_file_budget_violation" << std::endl;
}

void test_service_override() {
    std::string common = R"(
common:
  log:
    schema_version: 1
    level: INFO
)";
    std::string service = R"(
log:
  level: DEBUG
  modules:
    transport: WARN
    uds: INFO
)";
    auto result = LogConfigAdapter::loadFromYamlString(common, service);
    assert(result.second.code == LogError::kOk);
    assert(result.first.level == LogLevel::kDebug);
    assert(result.first.module_levels["transport"] == LogLevel::kWarn);
    assert(result.first.module_levels["uds"] == LogLevel::kInfo);
    std::cout << "  [PASS] test_service_override" << std::endl;
}

void test_default_degradation_on_error() {
    std::string yaml = "invalid: yaml: [[[";  // 格式错误
    auto result = LogConfigAdapter::loadFromYamlString(yaml);
    assert(result.second.code == LogError::kConfigInvalid);
    assert(result.first.level == LogLevel::kInfo);
    assert(result.first.console_config.enabled == true);
    std::cout << "  [PASS] test_default_degradation_on_error" << std::endl;
}

void test_default_format_is_standard() {
    std::string yaml = R"(
common:
  log:
    level: INFO
)";
    auto result = LogConfigAdapter::loadFromYamlString(yaml);
    assert(result.second.code == LogError::kOk);
    assert(result.first.format == "standard");
    std::cout << "  [PASS] test_default_format_is_standard" << std::endl;
}

void test_json_format_config() {
    std::string yaml = R"(
common:
  log:
    level: INFO
    format: json
)";
    auto result = LogConfigAdapter::loadFromYamlString(yaml);
    assert(result.second.code == LogError::kOk);
    assert(result.first.format == "json");
    std::cout << "  [PASS] test_json_format_config" << std::endl;
}

void test_invalid_format_config() {
    std::string yaml = R"(
common:
  log:
    level: INFO
    format: xml
)";
    auto result = LogConfigAdapter::loadFromYamlString(yaml);
    assert(result.second.code == LogError::kConfigInvalid);
    assert(result.second.message.find("format") != std::string::npos);
    std::cout << "  [PASS] test_invalid_format_config" << std::endl;
}

void test_service_level_format_override() {
    std::string common = R"(
common:
  log:
    level: INFO
    format: standard
)";
    std::string service = R"(
log:
  format: json
)";
    auto result = LogConfigAdapter::loadFromYamlString(common, service);
    assert(result.second.code == LogError::kOk);
    assert(result.first.format == "json");
    std::cout << "  [PASS] test_service_level_format_override" << std::endl;
}

int main() {
    std::cout << "Running LogConfigAdapter tests..." << std::endl;
    test_default_config();
    test_valid_config();
    test_invalid_schema_version();
    test_file_budget_violation();
    test_service_override();
    test_default_degradation_on_error();
    test_default_format_is_standard();
    test_json_format_config();
    test_invalid_format_config();
    test_service_level_format_override();
    std::cout << "All LogConfigAdapter tests passed!" << std::endl;
    return 0;
}
