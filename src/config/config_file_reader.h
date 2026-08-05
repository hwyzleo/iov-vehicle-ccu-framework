#pragma once

#include "config_types.h"
#include <filesystem>
#include <string>
#include <cstdint>

namespace cgw {
namespace fw {
namespace config {

// Reads a single config file with a size cap.
// Throws ConfigException(CGW-FW-0001) on open/seek/size/read failure.
class FileReader {
public:
    static constexpr uint64_t kMaxFileSize = 1u * 1024u * 1024u; // 1 MiB

    static std::string read(const std::filesystem::path& path);
};

} // namespace config
} // namespace fw
} // namespace cgw
