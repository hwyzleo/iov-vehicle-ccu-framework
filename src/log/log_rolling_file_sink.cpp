#include "log_rolling_file_sink.h"
#include <sys/stat.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <climits>

namespace ccu {
namespace fw {
namespace log {

RollingFileSink::RollingFileSink(const FileConfig& config, const std::string& serviceName)
    : m_config(config)
    , m_serviceName(serviceName)
{
    std::string dir = m_config.root + "/" + m_serviceName;
    mkdir(dir.c_str(), 0755);
    m_available = openNewFile();
}

RollingFileSink::~RollingFileSink() {
    if (m_file) {
        fclose(m_file);
        m_file = nullptr;
    }
}

bool RollingFileSink::write(const std::string& line) {
    if (!m_available || !m_file) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    std::string output = line + "\n";
    size_t written = fwrite(output.c_str(), 1, output.size(), m_file);
    if (written != output.size()) {
        m_available = false;
        return false;
    }

    m_currentSize += written;
    rotateIfNeeded();
    return true;
}

void RollingFileSink::flush() {
    if (m_file) {
        fflush(m_file);
    }
}

bool RollingFileSink::isAvailable() const {
    return m_available;
}

int RollingFileSink::cleanup() {
    std::string dir = m_config.root + "/" + m_serviceName;
    DIR* d = opendir(dir.c_str());
    if (!d) return 0;

    int removed = 0;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        if (name.find(m_serviceName + "_") == 0 && name.find(".log") != std::string::npos) {
            std::string path = dir + "/" + name;
            if (remove(path.c_str()) == 0) {
                ++removed;
            }
        }
    }
    closedir(d);
    return removed;
}

bool RollingFileSink::openNewFile() {
    if (m_file) {
        fclose(m_file);
        m_file = nullptr;
    }

    m_currentPath = getFilePath(m_currentIndex);
    m_file = fopen(m_currentPath.c_str(), "a");
    if (!m_file) {
        return false;
    }

    struct stat st;
    if (stat(m_currentPath.c_str(), &st) == 0) {
        m_currentSize = st.st_size;
    } else {
        m_currentSize = 0;
    }

    return true;
}

void RollingFileSink::rotateIfNeeded() {
    size_t maxSizeBytes = static_cast<size_t>(m_config.max_file_size_mb) * 1024 * 1024;
    if (m_currentSize < maxSizeBytes) return;

    uint32_t fileCount = countFiles();
    if (fileCount >= m_config.max_files) {
        removeOldestFile();
    }

    ++m_currentIndex;
    openNewFile();
}

std::string RollingFileSink::getFilePath(uint32_t index) const {
    std::ostringstream oss;
    oss << m_config.root << "/" << m_serviceName << "/" << m_serviceName << "_" << index << ".log";
    return oss.str();
}

void RollingFileSink::removeOldestFile() {
    std::string dir = m_config.root + "/" + m_serviceName;
    DIR* d = opendir(dir.c_str());
    if (!d) return;

    uint32_t minIndex = UINT32_MAX;
    std::string oldestFile;

    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        if (name.find(m_serviceName + "_") == 0 && name.find(".log") != std::string::npos) {
            size_t start = m_serviceName.size() + 1;
            size_t end = name.find(".log");
            if (end != std::string::npos) {
                std::string indexStr = name.substr(start, end - start);
                uint32_t idx = static_cast<uint32_t>(std::stoul(indexStr));
                if (idx < minIndex) {
                    minIndex = idx;
                    oldestFile = dir + "/" + name;
                }
            }
        }
    }
    closedir(d);

    if (!oldestFile.empty()) {
        remove(oldestFile.c_str());
    }
}

uint32_t RollingFileSink::countFiles() const {
    std::string dir = m_config.root + "/" + m_serviceName;
    DIR* d = opendir(dir.c_str());
    if (!d) return 0;

    uint32_t count = 0;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        if (name.find(m_serviceName + "_") == 0 && name.find(".log") != std::string::npos) {
            ++count;
        }
    }
    closedir(d);
    return count;
}

} // namespace log
} // namespace fw
} // namespace ccu
