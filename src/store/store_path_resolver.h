#pragma once

#include "store_types.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace ccu {
namespace fw {
namespace store {

namespace fs = std::filesystem;

// ============================================================
// PathResolver - root/service/key validation and path resolution
// ============================================================
// Layout: <root>/<svc>/<key>.dat            (0600, final)
//         <root>/<svc>/.<key>.<pid>.<n>.tmp (0600, temp, same directory)
//         <root>/<svc>/.<key>.lock          (cross-process lock)
// service/key reject empty, absolute paths, "..", "/" and "\". Dot-separated
// keys require every segment non-empty. Service directories are created with
// 0700 and verified not to be symlinks (escape prevention).
class PathResolver {
public:
    // non-empty, no '/' or '\', no "..", not "."
    static bool isValidServiceName(std::string_view service);

    // non-empty, dot-separated, every segment non-empty and not ".."/"."/",
    // no '/' or '\', not absolute.
    static bool isValidKey(std::string_view key);

    // Lexical normalization; reject empty / unrepresentable paths (0101).
    static fs::path normalizeRoot(const fs::path& root);

    // <root>/<svc>
    static fs::path serviceDir(const fs::path& root, std::string_view service);

    // <serviceDir>/<key>.dat
    static fs::path finalFile(const fs::path& serviceDir, std::string_view key);

    // <serviceDir>/.<key>.lock
    static fs::path lockFile(const fs::path& serviceDir, std::string_view key);

    // Create <root>/<svc> with 0700, verify it is a directory and not a
    // symlink. Throws CCU-FW-0101 on any path/permission/symlink failure.
    static void ensureServiceDir(const fs::path& root, std::string_view service,
                                 std::string_view svcForError = "");
};

} // namespace store
} // namespace fw
} // namespace ccu
