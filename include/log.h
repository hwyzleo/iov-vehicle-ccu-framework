#pragma once

#include "log_types.h"
#include <string>
#include <memory>
#include <initializer_list>

namespace ccu {
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
} // namespace ccu
