#pragma once

#include "log_types.h"
#include <string>
#include <unordered_map>

namespace ccu {
namespace fw {
namespace log {

class LevelFilter {
public:
    explicit LevelFilter(const LogConfig& config);

    bool shouldLog(LogLevel level, const std::string& module) const;
    void setGlobalLevel(LogLevel level);
    void setModuleLevel(const std::string& module, LogLevel level);

private:
    LogLevel m_globalLevel;
    std::unordered_map<std::string, LogLevel> m_moduleLevels;
};

} // namespace log
} // namespace fw
} // namespace ccu
