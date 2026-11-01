#pragma once

#include "log_types.h"
#include <string>
#include <chrono>
#include <vector>

namespace ccu {
namespace fw {
namespace log {

class Enricher {
public:
    Enricher(const std::string& service);

    // 为一条日志记录补齐公共字段
    std::vector<Field> enrich(
        std::vector<Field> fields,
        LogLevel level,
        const std::string& module,
        const std::string& event,
        const std::string& message,
        const LogContext* context = nullptr
    ) const;

private:
    std::string m_service;
    std::chrono::steady_clock::time_point m_startTime;
    pid_t m_pid;

    int64_t getMonoMs() const;
    std::string getTimestampUTC() const;
    bool isTimeSynced() const;
};

} // namespace log
} // namespace fw
} // namespace ccu
