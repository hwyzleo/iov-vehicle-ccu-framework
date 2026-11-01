#pragma once

#include "store_types.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ccu {
namespace fw {
namespace store {

namespace fs = std::filesystem;

// Atomic, crash-safe writer. Creates a unique temp file in the SAME directory
// as the final file (0600, O_CREAT|O_EXCL|O_NOFOLLOW), writes all bytes
// (short-write/EINTR safe), fsync(temp), renames temp -> final, then fsyncs
// the service directory. On pre-rename failure the temp is unlinked and the
// old final file is preserved. On post-rename dir-fsync failure the new value
// is in place but durability is unconfirmed (CCU-FW-0102, stage "dir-fsync").
class AtomicWriter {
public:
    static void commit(const fs::path& finalPath, const fs::path& serviceDir,
                       std::string_view service, std::string_view key,
                       const std::vector<uint8_t>& bytes,
                       std::size_t maxValueBytes);

    // fsync a directory by path (used by remove()). Throws CCU-FW-0102.
    static void syncDir(const fs::path& dir, std::string_view service,
                        std::string_view key, std::string_view stage);
};

} // namespace store
} // namespace fw
} // namespace ccu
