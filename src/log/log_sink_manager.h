#pragma once

#include "log_types.h"
#include "log_console_sink.h"
#include "log_rolling_file_sink.h"
#include <memory>
#include <mutex>
#include <atomic>

namespace ccu {
namespace fw {
namespace log {

class SinkManager {
public:
    SinkManager(const LogConfig& config, const std::string& serviceName);
    ~SinkManager();

    bool write(const std::string& line, LogLevel level = LogLevel::kInfo);
    void flush();
    bool hasAvailableSink() const;

private:
    std::unique_ptr<ConsoleSink> m_consoleSink;
    std::unique_ptr<RollingFileSink> m_fileSink;
    mutable std::mutex m_mutex;
    std::atomic<bool> m_stderrFallback{false};
    int m_consecutiveFailures = 0;

    void tryRecoverFileSink();
};

} // namespace log
} // namespace fw
} // namespace ccu
