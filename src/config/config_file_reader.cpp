#include "config_file_reader.h"
#include "config_error_formatter.h"
#include <fstream>
#include <system_error>

namespace ccu {
namespace fw {
namespace config {

namespace fs = std::filesystem;

std::string FileReader::read(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw ConfigException(
            "CCU-FW-0001",
            ConfigErrorFormatter::format(
                "CCU-FW-0001", "cannot open config file", path.string(), "", ""),
            path.string(), "");
    }

    f.seekg(0, std::ios::end);
    if (!f) {
        throw ConfigException(
            "CCU-FW-0001",
            ConfigErrorFormatter::format(
                "CCU-FW-0001", "cannot seek config file", path.string(), "", ""),
            path.string(), "");
    }
    std::streamoff size = f.tellg();
    if (size < 0) {
        throw ConfigException(
            "CCU-FW-0001",
            ConfigErrorFormatter::format(
                "CCU-FW-0001", "cannot determine config file size", path.string(), "", ""),
            path.string(), "");
    }
    if (static_cast<uint64_t>(size) > kMaxFileSize) {
        throw ConfigException(
            "CCU-FW-0001",
            ConfigErrorFormatter::format(
                "CCU-FW-0001", "config file exceeds size limit", path.string(), "", ""),
            path.string(), "");
    }

    f.seekg(0, std::ios::beg);
    std::string content;
    content.resize(static_cast<size_t>(size));
    if (size > 0) {
        f.read(&content[0], size);
        if (!f) {
            throw ConfigException(
                "CCU-FW-0001",
                ConfigErrorFormatter::format(
                    "CCU-FW-0001", "config file read failed", path.string(), "", ""),
                path.string(), "");
        }
    }
    return content;
}

} // namespace config
} // namespace fw
} // namespace ccu
