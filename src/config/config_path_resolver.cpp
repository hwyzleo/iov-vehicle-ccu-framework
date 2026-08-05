#include "config_path_resolver.h"
#include "config_error_formatter.h"
#include <system_error>
#include <set>

namespace cgw {
namespace fw {
namespace config {

namespace fs = std::filesystem;

namespace {

bool pathExists(const fs::path& p) {
    std::error_code ec;
    auto st = fs::status(p, ec);
    if (ec) return false;
    return st.type() != fs::file_type::not_found && st.type() != fs::file_type::none;
}

bool isRegularFile(const fs::path& p) {
    std::error_code ec;
    auto st = fs::status(p, ec);
    if (ec) return false;
    return st.type() == fs::file_type::regular;
}

// Check a found candidate: must be a regular file, else 0001 (no fallback).
void requireRegular(const fs::path& p, std::string_view source) {
    if (!isRegularFile(p)) {
        throw ConfigException(
            "CGW-FW-0001",
            ConfigErrorFormatter::format(
                "CGW-FW-0001",
                "config candidate exists but is not a regular file",
                p.string(), std::string(source), ""),
            p.string(), "");
    }
}

} // namespace

bool PathResolver::isValidServiceName(std::string_view service) {
    if (service.empty()) return false;
    if (service == ".") return false;
    if (service.find('/') != std::string_view::npos) return false;
    if (service.find('\\') != std::string_view::npos) return false;
    if (service.find("..") != std::string_view::npos) return false;
    return true;
}

fs::path PathResolver::normalizeRoot(const fs::path& root) {
    if (root.empty()) {
        throw ConfigException(
            "CGW-FW-0004",
            ConfigErrorFormatter::format(
                "CGW-FW-0004", "empty config root", "", "", "configRoots"),
            "", "configRoots");
    }
    fs::path p = root.lexically_normal();
    // Strip trailing separators: lexically_normal may retain a trailing slash,
    // which would break de-dup and join semantics (must not depend on trailing /).
    while (!p.empty() && p != p.root_path() && !p.has_filename()) {
        p = p.parent_path();
    }
    if (p.empty()) {
        throw ConfigException(
            "CGW-FW-0004",
            ConfigErrorFormatter::format(
                "CGW-FW-0004", "illegal config root: " + root.string(), "", "", "configRoots"),
            "", "configRoots");
    }
    return p;
}

std::vector<fs::path> PathResolver::normalizeRoots(const std::vector<fs::path>& roots) {
    std::vector<fs::path> out;
    std::set<std::string> seen;
    for (const auto& r : roots) {
        fs::path p = normalizeRoot(r);
        if (seen.insert(p.generic_string()).second) {
            out.push_back(std::move(p));
        }
    }
    return out;
}

fs::path PathResolver::normalizeCwd(const fs::path& cwd) {
    if (cwd.empty()) {
        throw ConfigException(
            "CGW-FW-0004",
            ConfigErrorFormatter::format(
                "CGW-FW-0004", "empty cwd", "", "", "cwd"),
            "", "cwd");
    }
    return cwd.lexically_normal();
}

std::vector<ConfigEntry> PathResolver::resolveAll(
    std::string_view service,
    const std::vector<fs::path>& configRoots,
    const fs::path& cwd) {

    const std::string svc(service);
    const std::string svcFile = svc + ".yaml";
    std::vector<ConfigEntry> entries;

    // Stage 1: first-match common.yaml (required).
    {
        fs::path common;
        bool found = false;
        for (const auto& root : configRoots) {
            fs::path p = root / "common.yaml";
            if (pathExists(p)) {
                requireRegular(p, "common");
                common = p;
                found = true;
                break; // first-match
            }
        }
        if (!found) {
            throw ConfigException(
                "CGW-FW-0001",
                ConfigErrorFormatter::format(
                    "CGW-FW-0001",
                    "required common.yaml not found in any config root",
                    "", "common", ""),
                "", "common.yaml");
        }
        entries.push_back({common, "common"});
    }

    // Stage 2: ./config/common.yaml (optional).
    {
        fs::path p = cwd / "config" / "common.yaml";
        if (pathExists(p)) {
            requireRegular(p, "project/common");
            entries.push_back({p, "project/common"});
        }
    }

    // Stage 3: first-match conf.d/<svc>.yaml (optional).
    {
        fs::path hit;
        bool found = false;
        for (const auto& root : configRoots) {
            fs::path p = root / "conf.d" / svcFile;
            if (pathExists(p)) {
                requireRegular(p, "conf.d/" + svc);
                hit = p;
                found = true;
                break; // first-match
            }
        }
        if (found) {
            entries.push_back({hit, "conf.d/" + svc});
        }
    }

    // Stage 4: ./config/<svc>.yaml (optional).
    {
        fs::path p = cwd / "config" / svcFile;
        if (pathExists(p)) {
            requireRegular(p, "project/" + svc);
            entries.push_back({p, "project/" + svc});
        }
    }

    // Stage 5: ordered-overlay configRoots[i]/<svc>.yaml (optional, all roots).
    for (size_t i = 0; i < configRoots.size(); ++i) {
        fs::path p = configRoots[i] / svcFile;
        if (pathExists(p)) {
            std::string source = "root[" + std::to_string(i) + "]/" + svc;
            requireRegular(p, source);
            entries.push_back({p, source});
        }
    }

    // Stage 6: ./<svc>.yaml (optional, highest priority).
    {
        fs::path p = cwd / svcFile;
        if (pathExists(p)) {
            requireRegular(p, "local/" + svc);
            entries.push_back({p, "local/" + svc});
        }
    }

    return entries;
}

} // namespace config
} // namespace fw
} // namespace cgw
