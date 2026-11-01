#pragma once

#include "log_types.h"
#include <string>

namespace ccu {
namespace fw {
namespace log {

class ConsoleSink {
public:
    ConsoleSink();
    ~ConsoleSink();

    bool write(const std::string& line, LogLevel level = LogLevel::kInfo);
    void flush();
    bool isAvailable() const;

private:
    bool m_available = true;

    // 根据日志级别返回 ANSI 颜色码，非终端时返回空字符串
    static const char* ansiColorForLevel(LogLevel level);
};

} // namespace log
} // namespace fw
} // namespace ccu
