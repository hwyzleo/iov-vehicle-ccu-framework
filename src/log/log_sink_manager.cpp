#include "log_sink_manager.h"
#include <cstdio>

namespace cgw {
namespace fw {
namespace log {

SinkManager::SinkManager(const LogConfig& config, const std::string& serviceName) {
    if (config.console_config.enabled) {
        m_consoleSink.reset(new ConsoleSink());
    }
    if (config.file_config.enabled) {
        m_fileSink.reset(new RollingFileSink(config.file_config, serviceName));
    }
}

SinkManager::~SinkManager() {
    flush();
}

bool SinkManager::write(const std::string& line, LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    bool anySuccess = false;

    if (m_consoleSink && m_consoleSink->isAvailable()) {
        if (m_consoleSink->write(line, level)) {
            anySuccess = true;
        }
    }

    if (m_fileSink && m_fileSink->isAvailable()) {
        if (m_fileSink->write(line)) {
            anySuccess = true;
            m_consecutiveFailures = 0;
        } else {
            ++m_consecutiveFailures;
            if (m_consecutiveFailures >= 3) {
                m_stderrFallback = true;
            }
        }
    }

    if (!anySuccess) {
        std::string fallback = "[LOG_FALLBACK] " + line + "\n";
        fwrite(fallback.c_str(), 1, fallback.size(), stderr);
        anySuccess = true;
    }

    return anySuccess;
}

void SinkManager::flush() {
    if (m_consoleSink) m_consoleSink->flush();
    if (m_fileSink) m_fileSink->flush();
}

bool SinkManager::hasAvailableSink() const {
    if (m_consoleSink && m_consoleSink->isAvailable()) return true;
    if (m_fileSink && m_fileSink->isAvailable()) return true;
    return m_stderrFallback;
}

void SinkManager::tryRecoverFileSink() {
    // 退避探测恢复（简化实现）
}

} // namespace log
} // namespace fw
} // namespace cgw
