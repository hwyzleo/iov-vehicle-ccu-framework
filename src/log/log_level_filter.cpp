#include "log_level_filter.h"

namespace ccu {
namespace fw {
namespace log {

LevelFilter::LevelFilter(const LogConfig& config)
    : m_globalLevel(config.level)
    , m_moduleLevels(config.module_levels)
{
}

bool LevelFilter::shouldLog(LogLevel level, const std::string& module) const {
    auto it = m_moduleLevels.find(module);
    if (it != m_moduleLevels.end()) {
        return static_cast<uint8_t>(level) >= static_cast<uint8_t>(it->second);
    }
    return static_cast<uint8_t>(level) >= static_cast<uint8_t>(m_globalLevel);
}

void LevelFilter::setGlobalLevel(LogLevel level) {
    m_globalLevel = level;
}

void LevelFilter::setModuleLevel(const std::string& module, LogLevel level) {
    m_moduleLevels[module] = level;
}

} // namespace log
} // namespace fw
} // namespace ccu
