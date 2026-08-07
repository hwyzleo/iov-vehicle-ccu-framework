#pragma once

#include "store_types.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cgw {
namespace fw {
namespace store {

namespace fs = std::filesystem;

// Reads the committed final file with a size cap. Opens with O_NOFOLLOW
// (rejects symlinks). Maps ENOENT -> CGW-FW-0105, symlink/non-regular/size ->
// CGW-FW-0101/0104, other I/O -> CGW-FW-0102. Never reads temp files.
class FileReader {
public:
    static std::vector<uint8_t>
    read(const fs::path& path, std::size_t maxValueBytes,
         std::string_view service, std::string_view key);
};

} // namespace store
} // namespace fw
} // namespace cgw
