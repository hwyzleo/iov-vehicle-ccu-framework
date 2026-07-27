#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>

namespace cgw {
namespace fw {
namespace log {

// ============================================================
// 错误码定义 (CGW-FW-0201~0205)
// ============================================================
enum class LogError : uint32_t {
    kOk = 0,
    kConfigInvalid = 201,       // FW-0201: 配置解析/校验失败
    kInitFailed = 202,          // FW-0202: registry/队列/worker 初始化失败
    kSinkFailed = 203,          // FW-0203: sink 打开/写入/flush/滚动失败
    kQueueOverflow = 204,       // FW-0204: 异步队列溢出
    kSensitiveViolation = 205   // FW-0205: 字段违反敏感信息策略
};

// 错误信息结构
struct LogErrorInfo {
    LogError code;
    std::string message;
    std::string detail;  // 附加上下文（字段名、配置路径等）

    LogErrorInfo() : code(LogError::kOk) {}
    LogErrorInfo(LogError c, const std::string& m, const std::string& d = "")
        : code(c), message(m), detail(d) {}
};

// 日志异常类
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
    kOff = 6    // 关闭所有日志
};

// 字符串转 LogLevel（用于配置解析）
LogLevel logLevelFromString(const std::string& str);
// LogLevel 转字符串（用于 JSON 输出）
const char* logLevelToString(LogLevel level);

// ============================================================
// 敏感度分类
// ============================================================
enum class Sensitivity : uint8_t {
    Normal = 0,     // 普通字段，不脱敏
    Identifier = 1, // 标识符（VIN、device_sn等），默认掩码
    Payload = 2,    // 原始报文，限长+过滤
    Secret = 3      // 密钥/口令/Token，直接拒绝
};

// ============================================================
// 字段值类型（简化版，避免依赖 std::variant（C++17））
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
    std::string stringVal; // 用于 kString 类型

    // 便捷构造
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
// 日志配置（从 common.log.* 读取）
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
    std::string root = "/var/log/cgw";
    uint32_t max_file_size_mb = 20;
    uint32_t max_files = 5;
    uint32_t total_budget_mb = 100;
};

struct RedactConfig {
    std::string identifiers = "mask";       // mask / reject / hash
    uint32_t raw_payload_max_bytes = 256;
};

struct LogConfig {
    uint32_t schema_version = 1;
    LogLevel level = LogLevel::kInfo;
    bool strict = false;                    // 严格模式：初始化失败则 fail-closed
    AsyncConfig async_config;
    ConsoleConfig console_config;
    FileConfig file_config;
    RedactConfig redact_config;
    std::string format = "standard";       // "standard" 或 "json"
    // 模块级别覆盖: <module> -> LogLevel
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
