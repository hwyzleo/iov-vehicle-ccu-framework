# CGW Framework Log 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 实现 CGW 统一日志库 `cgw-framework-log`，为 CGW 服务提供结构化日志、SOME/IP/诊断上下文、脱敏和异步能力。

**架构：** 分层架构 - 自研 Logger facade + ContextScope + 异步队列，底层使用 spdlog + fmt 实现 sink 写入。复用 TBOX 代码结构，修改命名空间和适配 CGW 特有字段。

**技术栈：** C++11, spdlog, fmt, yaml-cpp, CMake

---

## 文件结构

### 头文件（include/）
- `log.h` - Logger facade + ContextScope 定义
- `log_types.h` - 错误码、配置结构、字段类型

### 源文件（src/log/）
- `log_types.cpp` - 类型工具函数实现
- `log_config_adapter.cpp` - YAML 配置解析与校验
- `log_enricher.cpp` - 字段补齐（时间、进程、SOME/IP、诊断）
- `log_redactor.cpp` - 脱敏处理
- `log_level_filter.cpp` - 级别过滤
- `log_async_dispatcher.cpp` - 有界异步队列
- `log_json_formatter.cpp` - JSON Lines 格式化
- `log_console_sink.cpp` - stdout/stderr sink
- `log_rolling_file_sink.cpp` - 滚动文件 sink
- `log_sink_manager.cpp` - sink 管理与故障隔离
- `log_emergency_writer.cpp` - 紧急 stderr 写入
- `log_logger.cpp` - LoggerRegistry + Logger 实现

### 测试文件（tests/）
- `test_log_types.cpp` - 类型工具函数测试
- `test_log_config_adapter.cpp` - 配置解析测试
- `test_log_enricher.cpp` - 字段补齐测试
- `test_log_redactor.cpp` - 脱敏测试
- `test_log_level_filter.cpp` - 级别过滤测试
- `test_log_json_formatter.cpp` - JSON 格式化测试
- `test_log_async_dispatcher.cpp` - 异步队列测试
- `test_log_context_scope.cpp` - 上下文传播测试
- `test_log_integration.cpp` - 集成测试

---

## 任务 1：项目初始化与 CMake 配置

**文件：**
- 创建：`CMakeLists.txt`
- 创建：`.gitignore`
- 创建：`include/log_types.h`
- 创建：`include/log.h`

- [ ] **步骤 1：从 TBOX 复制基础文件结构**

```bash
# 复制 third_party 目录
cp -r /Users/hwyz_leo/Projects/open-iov/vehicle/tbox/iov-vehicle-tbox-framework/third_party .

# 复制 CMakeLists.txt 作为基础
cp /Users/hwyz_leo/Projects/open-iov/vehicle/tbox/iov-vehicle-tbox-framework/CMakeLists.txt .
```

- [ ] **步骤 2：修改 CMakeLists.txt**

将项目名称从 `Framework` 改为 `CGW-Framework`，库名保持 `hwyz`：

```cmake
project(CGW-Framework)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE FRAMEWORK_SOURCES src/*.cpp)
file(GLOB_RECURSE FRAMEWORK_HEADERS include/*.h)

add_library(hwyz SHARED
        ${FRAMEWORK_SOURCES}
        ${FRAMEWORK_HEADERS}
        )
set_target_properties(hwyz PROPERTIES
        LINKER_LANGUAGE CXX
        )
```

- [ ] **步骤 3：验证 CMake 配置**

```bash
mkdir -p build && cd build
cmake ..
make -j4
```

预期：编译成功（可能有警告，因为源文件为空）

- [ ] **步骤 4：Commit**

```bash
git add CMakeLists.txt third_party/
git commit -m "chore: initialize CGW framework project with CMake and third_party"
```

---

## 任务 2：定义 log_types.h（类型与配置）

**文件：**
- 创建：`include/log_types.h`
- 创建：`tests/test_log_types.cpp`

- [ ] **步骤 1：编写 log_types.h**

从 TBOX 复制并修改命名空间 `tbox::fw::log` → `cgw::fw::log`，修改默认路径 `/var/log/tbox` → `/var/log/cgw`：

```cpp
#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace cgw {
namespace fw {
namespace log {

// ============================================================
// 错误码定义 (CGW-FW-0201~0205)
// ============================================================
enum class LogError : uint32_t {
    kOk = 0,
    kConfigInvalid = 201,       // CGW-FW-0201: 配置解析/校验失败
    kInitFailed = 202,          // CGW-FW-0202: registry/队列/worker 初始化失败
    kSinkFailed = 203,          // CGW-FW-0203: sink 打开/写入/flush/滚动失败
    kQueueOverflow = 204,       // CGW-FW-0204: 异步队列溢出
    kSensitiveViolation = 205   // CGW-FW-0205: 字段违反敏感信息策略
};

struct LogErrorInfo {
    LogError code;
    std::string message;
    std::string detail;

    LogErrorInfo() : code(LogError::kOk) {}
    LogErrorInfo(LogError c, const std::string& m, const std::string& d = "")
        : code(c), message(m), detail(d) {}
};

class LogException : public std::exception {
public:
    LogException(LogError code, const std::string& message, const std::string& detail = "")
        : m_error{code, message, detail} {}

    LogErrorInfo getError() const { return m_error; }
    const char* what() const noexcept override { return m_error.message.c_str(); }

private:
    LogErrorInfo m_error;
};

// ============================================================
// 日志级别
// ============================================================
enum class LogLevel : uint8_t {
    kTrace = 0,
    kDebug = 1,
    kInfo = 2,
    kWarn = 3,
    kError = 4,
    kFatal = 5,
    kOff = 6
};

LogLevel logLevelFromString(const std::string& str);
const char* logLevelToString(LogLevel level);

// ============================================================
// 敏感度分类
// ============================================================
enum class Sensitivity : uint8_t {
    Normal = 0,
    Identifier = 1,
    Payload = 2,
    Secret = 3
};

// ============================================================
// 字段值类型
// ============================================================
enum class FieldValueType : uint8_t {
    kString,
    kInt64,
    kDouble,
    kBool
};

struct FieldValue {
    FieldValueType type;
    union {
        int64_t intVal;
        double doubleVal;
        bool boolVal;
    };
    std::string stringVal;

    static FieldValue makeString(const std::string& v);
    static FieldValue makeInt(int64_t v);
    static FieldValue makeDouble(double v);
    static FieldValue makeBool(bool v);
};

// ============================================================
// 结构化字段
// ============================================================
struct Field {
    std::string key;
    FieldValue value;
    Sensitivity sensitivity;

    Field() : sensitivity(Sensitivity::Normal) {}
    Field(const std::string& k, const FieldValue& v, Sensitivity s = Sensitivity::Normal)
        : key(k), value(v), sensitivity(s) {}
};

// ============================================================
// SOME/IP 上下文（CGW 特有）
// ============================================================
struct SomeIpContext {
    std::string service_id;
    std::string instance_id;
    std::string method_id;
    std::string client_id;
    std::string remote_endpoint;
};

// ============================================================
// 诊断上下文（CGW 特有）
// ============================================================
struct DiagnosticContext {
    std::string ecu_id;
    std::string logical_address;
    std::string transport;
    std::string uds_sid;
    std::string did;
    std::string nrc;
    uint32_t attempt = 0;
};

// ============================================================
// 日志上下文（用于 ContextScope 传播）
// ============================================================
struct LogContext {
    std::string trace_id;
    std::string request_id;
    std::string session_id;
    std::unique_ptr<SomeIpContext> someip;
    std::unique_ptr<DiagnosticContext> diagnostic;
};

// ============================================================
// 日志配置
// ============================================================
struct AsyncConfig {
    bool enabled = true;
    uint32_t queue_size = 4096;
    uint32_t flush_interval_ms = 1000;
};

struct ConsoleConfig {
    bool enabled = true;
};

struct FileConfig {
    bool enabled = false;
    std::string root = "/var/log/cgw";  // CGW 特有路径
    uint32_t max_file_size_mb = 20;
    uint32_t max_files = 5;
    uint32_t total_budget_mb = 100;
};

struct RedactConfig {
    std::string identifiers = "mask";
    uint32_t payload_max_bytes = 256;  // CR 设计中的字段名
    bool payload_protocol_filter = true;
};

struct LogConfig {
    uint32_t schema_version = 1;
    LogLevel level = LogLevel::kInfo;
    bool strict = false;
    AsyncConfig async_config;
    ConsoleConfig console_config;
    FileConfig file_config;
    RedactConfig redact_config;
    std::unordered_map<std::string, LogLevel> module_levels;
};

// ============================================================
// 初始化结果
// ============================================================
struct InitResult {
    LogError error;
    std::string error_message;

    InitResult() : error(LogError::kOk) {}
    InitResult(LogError e, const std::string& msg = "") : error(e), error_message(msg) {}
};

} // namespace log
} // namespace fw
} // namespace cgw
```

- [ ] **步骤 2：编写 test_log_types.cpp**

```cpp
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
    assert(config.redact_config.payload_max_bytes == 256);
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
```

- [ ] **步骤 3：创建 src/log/log_types.cpp**

```cpp
#include "log_types.h"
#include <algorithm>
#include <cctype>

namespace cgw {
namespace fw {
namespace log {

LogLevel logLevelFromString(const std::string& str) {
    std::string upper = str;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    if (upper == "TRACE") return LogLevel::kTrace;
    if (upper == "DEBUG") return LogLevel::kDebug;
    if (upper == "INFO") return LogLevel::kInfo;
    if (upper == "WARN") return LogLevel::kWarn;
    if (upper == "ERROR") return LogLevel::kError;
    if (upper == "FATAL") return LogLevel::kFatal;
    if (upper == "OFF") return LogLevel::kOff;
    
    return LogLevel::kInfo;  // 默认
}

const char* logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::kTrace: return "TRACE";
        case LogLevel::kDebug: return "DEBUG";
        case LogLevel::kInfo: return "INFO";
        case LogLevel::kWarn: return "WARN";
        case LogLevel::kError: return "ERROR";
        case LogLevel::kFatal: return "FATAL";
        case LogLevel::kOff: return "OFF";
        default: return "INFO";
    }
}

FieldValue FieldValue::makeString(const std::string& v) {
    FieldValue fv;
    fv.type = FieldValueType::kString;
    fv.stringVal = v;
    return fv;
}

FieldValue FieldValue::makeInt(int64_t v) {
    FieldValue fv;
    fv.type = FieldValueType::kInt64;
    fv.intVal = v;
    return fv;
}

FieldValue FieldValue::makeDouble(double v) {
    FieldValue fv;
    fv.type = FieldValueType::kDouble;
    fv.doubleVal = v;
    return fv;
}

FieldValue FieldValue::makeBool(bool v) {
    FieldValue fv;
    fv.type = FieldValueType::kBool;
    fv.boolVal = v;
    return fv;
}

} // namespace log
} // namespace fw
} // namespace cgw
```

- [ ] **步骤 4：运行测试**

```bash
cd build
cmake ..
make -j4
./test_log_types
```

预期输出：
```
=== log_types tests ===
  [PASS] test_log_level_from_string
  [PASS] test_log_level_to_string
  [PASS] test_field_value_make
  [PASS] test_default_config
  [PASS] test_error_codes
=== All log_types tests passed ===
```

- [ ] **步骤 5：Commit**

```bash
git add include/log_types.h src/log/log_types.cpp tests/test_log_types.cpp
git commit -m "feat: add log_types.h with CGW-specific types and config structures"
```

---

## 任务 3：定义 log.h（Logger facade + ContextScope）

**文件：**
- 创建：`include/log.h`

- [ ] **步骤 1：编写 log.h**

```cpp
#pragma once

#include "log_types.h"
#include <string>
#include <memory>
#include <initializer_list>

namespace cgw {
namespace fw {
namespace log {

class LoggerRegistry;

// ============================================================
// Logger facade — 对外统一日志 API
// ============================================================
class Logger {
public:
    // 初始化日志系统（每个服务启动时调用一次）
    static InitResult init(const std::string& service, const LogConfig& config);

    // 获取指定模块的 Logger 实例
    static Logger get(const std::string& module);

    // 日志输出方法
    void trace(std::string_view event, std::string_view message,
               std::initializer_list<Field> fields = {});
    void debug(std::string_view event, std::string_view message,
               std::initializer_list<Field> fields = {});
    void info(std::string_view event, std::string_view message,
              std::initializer_list<Field> fields = {});
    void warn(std::string_view event, std::string_view message,
              std::initializer_list<Field> fields = {});
    void error(std::string_view event, std::string_view message,
               std::initializer_list<Field> fields = {});
    [[noreturn]] void fatal(std::string_view event, std::string_view message,
                            std::initializer_list<Field> fields = {});

    void flush();

private:
    Logger();
    class Impl;
    std::shared_ptr<Impl> m_impl;

    friend class LoggerRegistry;
};

// ============================================================
// ContextScope — RAII 上下文传播
// ============================================================
class ContextScope {
public:
    explicit ContextScope(LogContext context);
    ~ContextScope();

    ContextScope(const ContextScope&) = delete;
    ContextScope& operator=(const ContextScope&) = delete;

    static const LogContext* current();

private:
    LogContext m_context;
    const LogContext* m_previous;
};

} // namespace log
} // namespace fw
} // namespace cgw
```

- [ ] **步骤 2：Commit**

```bash
git add include/log.h
git commit -m "feat: add log.h with Logger facade and ContextScope declarations"
```

---

## 任务 4：实现 LogConfigAdapter（配置解析）

**文件：**
- 创建：`src/log/log_config_adapter.h`
- 创建：`src/log/log_config_adapter.cpp`
- 创建：`tests/test_log_config_adapter.cpp`

- [ ] **步骤 1：编写 test_log_config_adapter.cpp**

```cpp
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
      payload_max_bytes: 512
)";
    auto result = LogConfigAdapter::loadFromYamlString(yaml);
    assert(result.second.code == LogError::kOk);
    assert(result.first.level == LogLevel::kWarn);
    assert(result.first.async_config.queue_size == 8192);
    assert(result.first.redact_config.payload_max_bytes == 512);
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
      root: /tmp/cgw_test
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
    // 应返回默认配置，不崩溃
    assert(result.first.schema_version == 1);
    assert(result.first.level == LogLevel::kInfo);
    std::cout << "  [PASS] test_default_degradation_on_error" << std::endl;
}

int main() {
    std::cout << "=== test_log_config_adapter ===" << std::endl;
    test_default_config();
    test_valid_config();
    test_invalid_schema_version();
    test_file_budget_violation();
    test_service_override();
    test_default_degradation_on_error();
    std::cout << "=== All config adapter tests passed ===" << std::endl;
    return 0;
}
```

- [ ] **步骤 2：编写 log_config_adapter.h**

```cpp
#pragma once

#include "log_types.h"
#include <string>
#include <utility>

namespace cgw {
namespace fw {
namespace log {

class LogConfigAdapter {
public:
    static std::pair<LogConfig, LogErrorInfo> loadFromYaml(
        const std::string& commonConfigPath,
        const std::string& serviceConfigPath = ""
    );

    static std::pair<LogConfig, LogErrorInfo> loadFromYamlString(
        const std::string& commonYaml,
        const std::string& serviceYaml = ""
    );

    static LogErrorInfo validate(const LogConfig& config);
    static LogConfig getDefaultConfig();

private:
    static LogLevel parseLevel(const std::string& str);
};

} // namespace log
} // namespace fw
} // namespace cgw
```

- [ ] **步骤 3：实现 log_config_adapter.cpp**

从 TBOX 复制并修改命名空间。

- [ ] **步骤 4：运行测试**

```bash
./test_log_config_adapter
```

预期：所有测试通过

- [ ] **步骤 5：Commit**

```bash
git add src/log/log_config_adapter.h src/log/log_config_adapter.cpp tests/test_log_config_adapter.cpp
git commit -m "feat: add LogConfigAdapter for YAML config parsing and validation"
```

---

## 任务 5：实现 LogEnricher（字段补齐）

**文件：**
- 创建：`src/log/log_enricher.h`
- 创建：`src/log/log_enricher.cpp`
- 创建：`tests/test_log_enricher.cpp`

- [ ] **步骤 1：编写 test_log_enricher.cpp**

```cpp
#include "log_types.h"
#include "log/log_enricher.h"
#include <cassert>
#include <iostream>

using namespace cgw::fw::log;

void test_basic_enrich() {
    Enricher enricher("cgw-diag");
    
    std::vector<Field> fields;
    fields.emplace_back("custom_key", FieldValue::makeString("value"));
    
    auto enriched = enricher.enrich("tester", "diag.uds.request", "test message", fields);
    
    // 检查必填字段
    assert(enriched.find("schema_version") != enriched.end());
    assert(enriched.find("timestamp") != enriched.end());
    assert(enriched.find("level") != enriched.end());
    assert(enriched["service"] == "cgw-diag");
    assert(enriched["module"] == "tester");
    assert(enriched["event"] == "diag.uds.request");
    assert(enriched["message"] == "test message");
    assert(enriched.find("pid") != enriched.end());
    assert(enriched.find("tid") != enriched.end());
    
    // 检查自定义字段
    assert(enriched["custom_key"] == "value");
    
    std::cout << "  [PASS] test_basic_enrich" << std::endl;
}

void test_someip_context() {
    Enricher enricher("cgw-diag");
    
    LogContext ctx;
    ctx.trace_id = "tr-123";
    ctx.request_id = "req-456";
    ctx.someip = std::make_unique<SomeIpContext>();
    ctx.someip->service_id = "0x1110";
    ctx.someip->method_id = "0x0001";
    
    std::vector<Field> fields;
    auto enriched = enricher.enrichWithContext("tester", ctx, fields);
    
    assert(enriched["trace_id"] == "tr-123");
    assert(enriched["request_id"] == "req-456");
    assert(enriched["someip_service_id"] == "0x1110");
    assert(enriched["someip_method_id"] == "0x0001");
    
    std::cout << "  [PASS] test_someip_context" << std::endl;
}

void test_diagnostic_context() {
    Enricher enricher("cgw-diag");
    
    LogContext ctx;
    ctx.diagnostic = std::make_unique<DiagnosticContext>();
    ctx.diagnostic->ecu_id = "BMS";
    ctx.diagnostic->logical_address = "0x001A";
    ctx.di diagnostic->transport = "DoCAN";
    ctx.diagnostic->uds_sid = "0x22";
    ctx.diagnostic->did = "F190";
    ctx.diagnostic->nrc = "0x31";
    ctx.diagnostic->attempt = 2;
    
    std::vector<Field> fields;
    auto enriched = enricher.enrichWithContext("tester", ctx, fields);
    
    assert(enriched["ecu_id"] == "BMS");
    assert(enriched["logical_address"] == "0x001A");
    assert(enriched["transport"] == "DoCAN");
    assert(enriched["uds_sid"] == "0x22");
    assert(enriched["did"] == "F190");
    assert(enriched["nrc"] == "0x31");
    assert(enriched["attempt"] == "2");
    
    std::cout << "  [PASS] test_diagnostic_context" << std::endl;
}

int main() {
    std::cout << "=== test_log_enricher ===" << std::endl;
    test_basic_enrich();
    test_someip_context();
    test_diagnostic_context();
    std::cout << "=== All enricher tests passed ===" << std::endl;
    return 0;
}
```

- [ ] **步骤 2：实现 log_enricher.h/cpp**

从 TBOX 复制，增加 SOME/IP 和诊断字段补齐逻辑。

- [ ] **步骤 3：运行测试**

```bash
./test_log_enricher
```

- [ ] **步骤 4：Commit**

```bash
git add src/log/log_enricher.h src/log/log_enricher.cpp tests/test_log_enricher.cpp
git commit -m "feat: add LogEnricher with SOME/IP and diagnostic context support"
```

---

## 任务 6：实现 LogRedactor（脱敏处理）

**文件：**
- 创建：`src/log/log_redactor.h`
- 创建：`src/log/log_redactor.cpp`
- 创建：`tests/test_log_redactor.cpp`

- [ ] **步骤 1：编写 test_log_redactor.cpp**

```cpp
#include "log_types.h"
#include "log/log_redactor.h"
#include <cassert>
#include <iostream>

using namespace cgw::fw::log;

void test_normal_field() {
    Redactor redactor({.identifiers = "mask", .payload_max_bytes = 256});
    
    Field field("key", FieldValue::makeString("value"), Sensitivity::Normal);
    auto result = redactor.process(field);
    assert(result.value.stringVal == "value");
    std::cout << "  [PASS] test_normal_field" << std::endl;
}

void test_identifier_masking() {
    Redactor redactor({.identifiers = "mask", .payload_max_bytes = 256});
    
    Field field("vin", FieldValue::makeString("WVWZZZ3CZWE123456"), Sensitivity::Identifier);
    auto result = redactor.process(field);
    // VIN 应该被掩码
    assert(result.value.stringVal.find("123456") == std::string::npos);
    assert(result.value.stringVal.find("***") != std::string::npos);
    std::cout << "  [PASS] test_identifier_masking" << std::endl;
}

void test_secret_rejection() {
    Redactor redactor({.identifiers = "mask", .payload_max_bytes = 256});
    
    Field field("password", FieldValue::makeString("my_secret_123"), Sensitivity::Secret);
    auto result = redactor.process(field);
    // 密钥应该被拒绝
    assert(result.value.stringVal.find("my_secret") == std::string::npos);
    assert(result.value.stringVal.find("REDACTED") != std::string::npos);
    std::cout << "  [PASS] test_secret_rejection" << std::endl;
}

void test_payload_truncation() {
    Redactor redactor({.identifiers = "mask", .payload_max_bytes = 10});
    
    std::string long_payload(100, 'A');
    Field field("payload", FieldValue::makeString(long_payload), Sensitivity::Payload);
    auto result = redactor.process(field);
    // Payload 应该被截断
    assert(result.value.stringVal.length() <= 20);  // 10 bytes + "..." + metadata
    std::cout << "  [PASS] test_payload_truncation" << std::endl;
}

void test_uds_seed_key_rejection() {
    Redactor redactor({.identifiers = "mask", .payload_max_bytes = 256});
    
    // UDS 0x27 seed 应该被拒绝
    Field field("uds_seed", FieldValue::makeString("AABBCCDD"), Sensitivity::Secret);
    auto result = redactor.process(field);
    assert(result.value.stringVal.find("AABB") == std::string::npos);
    std::cout << "  [PASS] test_uds_seed_key_rejection" << std::endl;
}

int main() {
    std::cout << "=== test_log_redactor ===" << std::endl;
    test_normal_field();
    test_identifier_masking();
    test_secret_rejection();
    test_payload_truncation();
    test_uds_seed_key_rejection();
    std::cout << "=== All redactor tests passed ===" << std::endl;
    return 0;
}
```

- [ ] **步骤 2：实现 log_redactor.h/cpp**

从 TBOX 复制，增加 CGW 特有脱敏规则：
- UDS 0x27 seed/key 拒绝
- 0x2E F190 (VIN) 脱敏
- 证书/密钥 provisioning 过滤

- [ ] **步骤 3：运行测试**

```bash
./test_log_redactor
```

- [ ] **步骤 4：Commit**

```bash
git add src/log/log_redactor.h src/log/log_redactor.cpp tests/test_log_redactor.cpp
git commit -m "feat: add LogRedactor with CGW-specific UDS/VIN/secret policies"
```

---

## 任务 7：实现 LogLevelFilter（级别过滤）

**文件：**
- 创建：`src/log/log_level_filter.h`
- 创建：`src/log/log_level_filter.cpp`
- 创建：`tests/test_log_level_filter.cpp`

- [ ] **步骤 1：编写测试和实现**

从 TBOX 复制，修改命名空间。

- [ ] **步骤 2：运行测试**

```bash
./test_log_level_filter
```

- [ ] **步骤 3：Commit**

```bash
git add src/log/log_level_filter.h src/log/log_level_filter.cpp tests/test_log_level_filter.cpp
git commit -m "feat: add LogLevelFilter with global/service/module level filtering"
```

---

## 任务 8：实现 AsyncDispatcher（异步队列）

**文件：**
- 创建：`src/log/log_async_dispatcher.h`
- 创建：`src/log/log_async_dispatcher.cpp`
- 创建：`tests/test_log_async_dispatcher.cpp`

- [ ] **步骤 1：编写测试和实现**

从 TBOX 复制，修改命名空间。

- [ ] **步骤 2：运行测试**

```bash
./test_log_async_dispatcher
```

- [ ] **步骤 3：Commit**

```bash
git add src/log/log_async_dispatcher.h src/log/log_async_dispatcher.cpp tests/test_log_async_dispatcher.cpp
git commit -m "feat: add AsyncDispatcher with bounded queue and priority handling"
```

---

## 任务 9：实现 JsonFormatter（JSON 格式化）

**文件：**
- 创建：`src/log/log_json_formatter.h`
- 创建：`src/log/log_json_formatter.cpp`
- 创建：`tests/test_json_formatter.cpp`

- [ ] **步骤 1：编写测试和实现**

从 TBOX 复制，修改命名空间。

- [ ] **步骤 2：运行测试**

```bash
./test_log_json_formatter
```

- [ ] **步骤 3：Commit**

```bash
git add src/log/log_json_formatter.h src/log/log_json_formatter.cpp tests/test_log_json_formatter.cpp
git commit -m "feat: add JsonLineFormatter for structured log output"
```

---

## 任务 10：实现 Sink（Console + Rolling File）

**文件：**
- 创建：`src/log/log_console_sink.h/cpp`
- 创建：`src/log/log_rolling_file_sink.h/cpp`
- 创建：`src/log/log_sink_manager.h/cpp`
- 创建：`src/log/log_emergency_writer.h/cpp`

- [ ] **步骤 1：从 TBOX 复制并修改命名空间**

- [ ] **步骤 2：Commit**

```bash
git add src/log/log_console_sink.* src/log/log_rolling_file_sink.* src/log/log_sink_manager.* src/log/log_emergency_writer.*
git commit -m "feat: add sink implementations (console, rolling file, manager, emergency)"
```

---

## 任务 11：实现 Logger（核心 LoggerRegistry）

**文件：**
- 创建：`src/log/log_logger.cpp`
- 创建：`tests/test_log_context_scope.cpp`
- 创建：`tests/test_log_integration.cpp`

- [ ] **步骤 1：实现 log_logger.cpp**

从 TBOX 复制，修改命名空间，集成所有组件。

- [ ] **步骤 2：编写 ContextScope 测试**

```cpp
#include "log.h"
#include <cassert>
#include <iostream>

using namespace cgw::fw::log;

void test_context_scope_basic() {
    LogContext ctx;
    ctx.trace_id = "tr-123";
    ctx.request_id = "req-456";
    
    assert(ContextScope::current() == nullptr);
    
    {
        ContextScope scope(std::move(ctx));
        assert(ContextScope::current() != nullptr);
        assert(ContextScope::current()->trace_id == "tr-123");
        assert(ContextScope::current()->request_id == "req-456");
    }
    
    assert(ContextScope::current() == nullptr);
    std::cout << "  [PASS] test_context_scope_basic" << std::endl;
}

void test_context_scope_nested() {
    LogContext ctx1;
    ctx1.trace_id = "tr-outer";
    
    LogContext ctx2;
    ctx2.trace_id = "tr-inner";
    ctx2.someip = std::make_unique<SomeIpContext>();
    ctx2.someip->service_id = "0x1110";
    
    {
        ContextScope scope1(std::move(ctx1));
        assert(ContextScope::current()->trace_id == "tr-outer");
        assert(ContextScope::current()->someip == nullptr);
        
        {
            ContextScope scope2(std::move(ctx2));
            assert(ContextScope::current()->trace_id == "tr-inner");
            assert(ContextScope::current()->someip != nullptr);
            assert(ContextScope::current()->someip->service_id == "0x1110");
        }
        
        assert(ContextScope::current()->trace_id == "tr-outer");
        assert(ContextScope::current()->someip == nullptr);
    }
    
    std::cout << "  [PASS] test_context_scope_nested" << std::endl;
}

int main() {
    std::cout << "=== test_log_context_scope ===" << std::endl;
    test_context_scope_basic();
    test_context_scope_nested();
    std::cout << "=== All context scope tests passed ===" << std::endl;
    return 0;
}
```

- [ ] **步骤 3：编写集成测试**

```cpp
#include "log.h"
#include "log/log_config_adapter.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdio>

using namespace cgw::fw::log;

void test_basic_logging() {
    // 使用默认配置初始化
    LogConfig config = LogConfigAdapter::getDefaultConfig();
    auto result = Logger::init("cgw-test", config);
    assert(result.error == LogError::kOk);
    
    Logger logger = Logger::get("tester");
    
    // 这些调用不应该崩溃
    logger.info("test.event", "Test message", {
        {"key", FieldValue::makeString("value")}
    });
    
    logger.warn("test.warning", "Warning message", {
        {"count", FieldValue::makeInt(42)}
    });
    
    logger.flush();
    
    std::cout << "  [PASS] test_basic_logging" << std::endl;
}

void test_logging_with_context() {
    LogConfig config = LogConfigAdapter::getDefaultConfig();
    Logger::init("cgw-test-ctx", config);
    
    Logger logger = Logger::get("tester");
    
    LogContext ctx;
    ctx.trace_id = "tr-integration";
    ctx.request_id = "req-001";
    ctx.someip = std::make_unique<SomeIpContext>();
    ctx.someip->service_id = "0x1110";
    ctx.someip->method_id = "0x0001";
    
    {
        ContextScope scope(std::move(ctx));
        logger.info("test.someip", "SOME/IP call", {
            {"result", FieldValue::makeString("success")}
        });
    }
    
    logger.flush();
    std::cout << "  [PASS] test_logging_with_context" << std::endl;
}

int main() {
    std::cout << "=== test_log_integration ===" << std::endl;
    test_basic_logging();
    test_logging_with_context();
    std::cout << "=== All integration tests passed ===" << std::endl;
    return 0;
}
```

- [ ] **步骤 4：运行所有测试**

```bash
cd build
cmake ..
make -j4
./test_log_context_scope
./test_log_integration
```

- [ ] **步骤 5：Commit**

```bash
git add src/log/log_logger.cpp tests/test_log_context_scope.cpp tests/test_log_integration.cpp
git commit -m "feat: add Logger implementation with ContextScope and integration tests"
```

---

## 任务 12：最终集成与验证

- [ ] **步骤 1：运行完整测试套件**

```bash
cd build
cmake ..
make -j4
ctest --output-on-failure
```

预期：所有测试通过

- [ ] **步骤 2：验证编译产物**

```bash
ls -la build/libhwyz.*
```

预期：生成 `libhwyz.dylib` (macOS) 或 `libhwyz.so` (Linux)

- [ ] **步骤 3：最终 Commit**

```bash
git add -A
git commit -m "feat: complete CGW framework-log implementation"
```

---

## 自检清单

- [x] 规格覆盖度：所有 CR 设计章节都有对应任务
- [x] 占位符扫描：无 TODO/待定占位符
- [x] 类型一致性：命名空间、类型名称在整个计划中一致
- [x] 测试覆盖：每个组件都有独立测试
- [x] TDD 原则：先写测试，再写实现
