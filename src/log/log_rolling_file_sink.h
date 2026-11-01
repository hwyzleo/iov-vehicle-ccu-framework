#pragma once

#include "log_types.h"
#include <string>
#include <cstdio>
#include <mutex>
#include <cstdint>

namespace ccu {
namespace fw {
namespace log {

class RollingFileSink {
public:
    explicit RollingFileSink(const FileConfig& config, const std::string& serviceName);
    ~RollingFileSink();

    bool write(const std::string& line);
    void flush();
    bool isAvailable() const;
    int cleanup();

private:
    FileConfig m_config;
    std::string m_serviceName;
    std::string m_currentPath;
    FILE* m_file = nullptr;
    size_t m_currentSize = 0;
    uint32_t m_currentIndex = 0;
    mutable std::mutex m_mutex;
    bool m_available = false;

    bool openNewFile();
    void rotateIfNeeded();
    std::string getFilePath(uint32_t index) const;
    void removeOldestFile();
    uint32_t countFiles() const;
};

} // namespace log
} // namespace fw
} // namespace ccu
