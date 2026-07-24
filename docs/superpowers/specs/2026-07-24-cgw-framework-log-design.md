# CGW Framework Log 设计规格

> CR: CGW-FW-DSN-CR-001  
> 日期: 2026-07-24  
> 状态: 设计批准，待实现

## 1. 概述

CGW framework 统一日志库 `cgw-framework-log`，为 CGW-DIAG、CGW-FOTA、CGW-SEC 及后续服务提供统一的日志初始化、级别、结构化字段、SOME/IP/诊断上下文、输出与滚动、敏感信息保护及异常降级能力。

### 1.1 服务形态

- **形态**: 进程内 C++ 静态库，无 daemon、无 IPC
- **库名**: `hwyz`（与 TBOX 保持一致）
- **命名空间**: `cgw::fw::log`
- **依赖**: spdlog + fmt（底层实现）、yaml-cpp（配置解析）

### 1.2 参考基线

- 参考项目: `iov-vehicle-tbox-framework`
- 架构模式: 自研 facade + spdlog 底层
- 代码结构: 复用 TBOX 目录结构和第三方库

## 2. 项目结构

```
iov-vehicle-cgw-framework/
├── CMakeLists.txt
├── include/
│   ├── log.h                   # Logger facade + ContextScope
│   ├── log_types.h             # 错误码、配置、字段类型
│   └── config.h                # 配置管理
├── src/
│   ├── log/
│   │   ├── log_logger.cpp
│   │   ├── log_types.cpp
│   │   ├── log_config_adapter.cpp
│   │   ├── log_enricher.cpp
│   │   ├── log_redactor.cpp
│   │   ├── log_level_filter.cpp
│   │   ├── log_async_dispatcher.cpp
│   │   ├── log_json_formatter.cpp
│   │   ├── log_console_sink.cpp
│   │   ├── log_rolling_file_sink.cpp
│   │   ├── log_sink_manager.cpp
│   │   └── log_emergency_writer.cpp
│   └── utils.cpp
├── tests/
│   ├── test_log_config_adapter.cpp
│   ├── test_log_enricher.cpp
│   ├── test_log_redactor.cpp
│   ├── test_log_level_filter.cpp
│   ├── test_log_json_formatter.cpp
│   ├── test_log_async_dispatcher.cpp
│   ├── test_log_context_scope.cpp
│   └── test_log_integration.cpp
└── third_party/
    ├── include/
    │   ├── spdlog/
    │   ├── yaml-cpp/
    │   └── fmt/
    └── lib/
        ├── arm64-apple-darwin/
        ├── aarch64-linux-gnu/
        ├── x86_64-apple-darwin/
        └── x86_64-linux-gnu/
```

## 3. API 设计

### 3.1 Logger Facade

```cpp
namespace cgw::fw::log {

class Logger {
public:
    static InitResult init(const std::string& service, const LogConfig& config);
    static Logger get(const std::string& module);

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
};

} // namespace cgw::fw::log
```

### 3.2 ContextScope

```cpp
namespace cgw::fw::log {

struct SomeIpContext {
    std::string service_id;
    std::string instance_id;
    std::string method_id;
    std::string client_id;
    std::string remote_endpoint;
};

struct DiagnosticContext {
    std::string ecu_id;
    std::string logical_address;
    std::string transport;
    std::string uds_sid;
    std::string did;
    std::string nrc;
    uint32_t attempt = 0;
};

struct LogContext {
    std::string trace_id;
    std::string request_id;
    std::string session_id;
    std::optional<SomeIpContext> someip;
    std::optional<DiagnosticContext> diagnostic;
};

class ContextScope {
public:
    explicit ContextScope(LogContext context);
    ~ContextScope();
    static const LogContext* current();
};

} // namespace cgw::fw::log
```

### 3.3 字段与敏感度

```cpp
namespace cgw::fw::log {

enum class Sensitivity : uint8_t {
    Normal = 0,
    Identifier = 1,  // VIN、device_sn 等，默认掩码
    Payload = 2,     // 原始报文，限长+过滤
    Secret = 3       // 密钥/口令/Token，直接拒绝
};

struct Field {
    std::string key;
    FieldValue value;
    Sensitivity sensitivity;
};

} // namespace cgw::fw::log
```

## 4. 配置模型

```yaml
common:
  log:
    schema_version: 1
    level: INFO
    strict: false
    async:
      enabled: true
      queue_size: 4096
      flush_interval_ms: 1000
    console:
      enabled: true
    file:
      enabled: false
      root: /var/log/cgw
      max_file_size_mb: 20
      max_files: 5
      total_budget_mb: 100
    redact:
      identifiers: mask
      payload_max_bytes: 256
      payload_protocol_filter: true

diag:
  log:
    level: INFO
    modules:
      router: WARN
      tester: INFO
      transport: WARN
```

## 5. 结构化日志字段

### 5.1 必填字段

| 字段 | 类型 | 说明 |
|------|------|------|
| schema_version | number | 固定值 1 |
| timestamp | string | UTC RFC3339 毫秒 |
| time_synced | boolean | 系统时间是否已同步 |
| mono_ms | number | 单调时钟毫秒 |
| level | string | TRACE/DEBUG/INFO/WARN/ERROR/FATAL |
| service | string | 服务名（如 cgw-diag） |
| module | string | 模块名 |
| event | string | 事件名（小写点分） |
| message | string | 人读消息 |
| pid | number | 进程 ID |
| tid | number | 线程 ID |

### 5.2 CGW 可选字段

**SOME/IP 上下文:**
- `someip_service_id`, `someip_instance_id`, `someip_method_id`, `someip_client_id`, `remote_endpoint`

**诊断上下文:**
- `ecu_id`, `logical_address`, `transport`, `uds_sid`, `did`, `nrc`, `attempt`

**通用上下文:**
- `error_code`, `trace_id`, `request_id`, `session_id`, `duration_ms`

## 6. 错误码

| 错误码 | 触发点 | 处理 |
|--------|--------|------|
| CGW-FW-0201 | 配置解析/校验失败 | 默认 console + INFO 降级；严格模式终止初始化 |
| CGW-FW-0202 | registry、队列或 worker 初始化失败 | 切换同步 stderr；严格模式 fail-closed |
| CGW-FW-0203 | sink 打开、写入、flush 或滚动失败 | 隔离故障 sink，切换 stderr |
| CGW-FW-0204 | 异步队列溢出 | 按级别丢弃/同步兜底 |
| CGW-FW-0205 | 字段违反敏感信息策略 | 拒绝原值并输出策略命中元数据 |

## 7. 实施计划

### Phase 1: 基础框架
- 从 TBOX 复制代码结构
- 修改命名空间 `tbox::fw::log` → `cgw::fw::log`
- 修改库名和 CMakeLists.txt
- 复制 third_party 目录

### Phase 2: ContextScope 扩展
- 增加 `SomeIpContext` 结构
- 增加 `DiagnosticContext` 结构
- 扩展 `LogContext` 包含可选上下文

### Phase 3: Enricher 适配
- 增加 SOME/IP 字段补齐
- 增加诊断字段补齐
- 保留通用字段补齐逻辑

### Phase 4: Redactor 适配
- 增加 UDS 0x27 seed/key 拒绝策略
- 增加 0x2E F190 (VIN) 脱敏策略
- 增加证书/密钥 provisioning 过滤
- 保留通用脱敏逻辑

### Phase 5: 配置与路径
- 修改文件路径 `/var/log/tbox/` → `/var/log/cgw/`
- 调整默认配置值

### Phase 6: 单元测试
- 逐组件编写测试
- 覆盖正常/异常/边界场景

### Phase 7: 集成测试
- 验证完整日志链路
- 验证 SOME/IP 上下文传播
- 验证诊断上下文传播
- 验证脱敏和降级

## 8. 测试策略

### 8.1 单元测试

| 测试文件 | 覆盖范围 |
|----------|----------|
| test_log_config_adapter.cpp | YAML 配置解析、校验、降级 |
| test_log_enricher.cpp | 字段补齐、SOME/IP/诊断字段 |
| test_log_redactor.cpp | 脱敏策略、UDS/VIN/密钥过滤 |
| test_log_level_filter.cpp | 全局/服务/模块级别过滤 |
| test_log_json_formatter.cpp | JSON 格式化、转义、字段顺序 |
| test_log_async_dispatcher.cpp | 队列满、溢出、高级别同步 |
| test_log_context_scope.cpp | 上下文传播、嵌套、恢复 |
| test_log_integration.cpp | 完整链路、端到端验证 |

### 8.2 测试风格

采用与 TBOX 一致的 assert 测试风格：

```cpp
#include "log_types.h"
#include "log/log_config_adapter.h"
#include <cassert>
#include <iostream>

void test_default_config() {
    LogConfig config = LogConfigAdapter::getDefaultConfig();
    assert(config.schema_version == 1);
    assert(config.level == LogLevel::kInfo);
    std::cout << "  [PASS] test_default_config" << std::endl;
}

int main() {
    test_default_config();
    // ... more tests
    return 0;
}
```

## 9. 跨平台支持

| 平台 | 编译器 | 三方库目录 |
|------|--------|-----------|
| macOS ARM64 (开发) | Apple Clang | arm64-apple-darwin |
| Linux aarch64 (目标) | GCC/ARM | aarch64-linux-gnu |
| macOS x86_64 | Apple Clang | x86_64-apple-darwin |
| Linux x86_64 (测试) | GCC | x86_64-linux-gnu |

## 10. 与 TBOX 的差异总结

| 维度 | TBOX | CGW |
|------|------|-----|
| 命名空间 | `tbox::fw::log` | `cgw::fw::log` |
| 库名 | hwyz | hwyz |
| ContextScope | trace/request/session | + SomeIpContext + DiagnosticContext |
| Enricher | 通用字段 | + SOME/IP/诊断字段 |
| Redactor | 通用脱敏 | + UDS/VIN/证书策略 |
| 文件路径 | `/var/log/tbox/` | `/var/log/cgw/` |
| 错误码前缀 | FW-020x | CGW-FW-020x |

---

## 规格自检清单

- [x] 无 TODO/待定占位符
- [x] 各章节内部一致
- [x] 聚焦单一实现计划可覆盖
- [x] 无二义性需求
