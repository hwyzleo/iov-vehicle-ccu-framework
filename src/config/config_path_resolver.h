#pragma once

#include "config_types.h"
#include <filesystem>
#include <vector>
#include <string>
#include <string_view>

namespace cgw {
namespace fw {
namespace config {

// ============================================================
// ConfigEntry - a resolved configuration file with a stable source label
// ============================================================
// source labels: "common", "project/common", "conf.d/<svc>",
//                "project/<svc>", "root[i]/<svc>", "local/<svc>"
struct ConfigEntry {
    std::filesystem::path path;
    std::string source;
};

// ============================================================
// PathResolver - multi-root six-stage resolveAll
// ============================================================
// Stages (later overrides earlier):
//   1. first(configRoots/*/common.yaml)            required (0001 if all absent)
//   2. ./config/common.yaml                        optional
//   3. first(configRoots/*/conf.d/<svc>.yaml)      optional
//   4. ./config/<svc>.yaml                         optional
//   5. configRoots[0..n-1]/<svc>.yaml              optional, ordered-overlay
//   6. ./<svc>.yaml                                optional, highest priority
// first-match (1,3): only the first existing regular file is taken.
// ordered-overlay (5): every existing <root>/<svc>.yaml is appended in index order.
// A found candidate that is not a regular file / unreadable / read-fails yields
// CGW-FW-0001 immediately, without falling back to later candidates.
class PathResolver {
public:
    // Validate service name: non-empty, no path separators, no '..', not ".".
    static bool isValidServiceName(std::string_view service);

    // Lexically normalize a single config root; reject empty/illegal (0004).
    static std::filesystem::path normalizeRoot(const std::filesystem::path& root);

    // Normalize and de-duplicate configRoots (keep first occurrence order).
    static std::vector<std::filesystem::path>
    normalizeRoots(const std::vector<std::filesystem::path>& roots);

    // Normalize cwd; reject empty (0004).
    static std::filesystem::path normalizeCwd(const std::filesystem::path& cwd);

    // Six-stage resolve. configRoots and cwd must be pre-normalized.
    // Throws ConfigException(CGW-FW-0001) for required-missing or found-but-invalid.
    static std::vector<ConfigEntry>
    resolveAll(std::string_view service,
               const std::vector<std::filesystem::path>& configRoots,
               const std::filesystem::path& cwd);
};

} // namespace config
} // namespace fw
} // namespace cgw
