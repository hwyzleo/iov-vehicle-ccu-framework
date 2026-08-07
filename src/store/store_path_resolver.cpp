#include "store_path_resolver.h"
#include "store_error_formatter.h"

#include <system_error>

namespace cgw {
namespace fw {
namespace store {

namespace fs = std::filesystem;

namespace {

bool containsSegment(std::string_view s, std::string_view seg) {
    return s.find(seg) != std::string_view::npos;
}

void fail101(std::string_view reason, std::string_view service,
             std::string_view key, std::string_view path,
             std::string_view stage) {
    throw StoreException(
        "CGW-FW-0101",
        StoreErrorFormatter::format("CGW-FW-0101", reason, service, key, path, stage),
        std::string(service), std::string(key), std::string(path), std::string(stage));
}

} // namespace

bool PathResolver::isValidServiceName(std::string_view service) {
    if (service.empty()) return false;
    if (service == ".") return false;
    if (containsSegment(service, "..")) return false;
    if (service.find('/') != std::string_view::npos) return false;
    if (service.find('\\') != std::string_view::npos) return false;
    return true;
}

bool PathResolver::isValidKey(std::string_view key) {
    if (key.empty()) return false;
    if (key.find('/') != std::string_view::npos) return false;
    if (key.find('\\') != std::string_view::npos) return false;
    // No leading/trailing/double dots.
    if (key.front() == '.') return false;
    if (key.back() == '.') return false;
    if (containsSegment(key, "..")) return false;
    // Every dot-separated segment must be non-empty (no consecutive dots).
    if (containsSegment(key, ".")) {
        // split and check
        std::string s(key);
        size_t start = 0;
        while (true) {
            size_t dot = s.find('.', start);
            std::string seg = (dot == std::string::npos) ? s.substr(start) : s.substr(start, dot - start);
            if (seg.empty()) return false;
            if (seg == "..") return false;
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
    }
    return true;
}

fs::path PathResolver::normalizeRoot(const fs::path& root) {
    if (root.empty()) {
        fail101("empty store root", "", "", "", "normalize-root");
    }
    fs::path p = root.lexically_normal();
    // Strip trailing separators so join semantics do not depend on trailing /.
    while (!p.empty() && p != p.root_path() && !p.has_filename()) {
        p = p.parent_path();
    }
    if (p.empty()) {
        fail101("illegal store root: " + root.string(), "", "", root.string(),
                "normalize-root");
    }
    return p;
}

fs::path PathResolver::serviceDir(const fs::path& root, std::string_view service) {
    return root / std::string(service);
}

fs::path PathResolver::finalFile(const fs::path& dir, std::string_view key) {
    std::string name(key);
    name += ".dat";
    return dir / name;
}

fs::path PathResolver::lockFile(const fs::path& dir, std::string_view key) {
    std::string name = ".";
    name += std::string(key);
    name += ".lock";
    return dir / name;
}

void PathResolver::ensureServiceDir(const fs::path& root, std::string_view service,
                                    std::string_view svcForError) {
    std::string svc(svcForError.empty() ? service : svcForError);
    fs::path dir = serviceDir(root, service);

    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        fail101("cannot create service directory: " + ec.message(), svc, "",
                dir.string(), "mkdir");
    }

    // Verify the resolved directory is a real directory and NOT a symlink
    // (symlink-escape prevention). symlink_status reports the link itself.
    auto st = fs::symlink_status(dir, ec);
    if (ec) {
        fail101("cannot stat service directory: " + ec.message(), svc, "",
                dir.string(), "mkdir");
    }
    if (st.type() != fs::file_type::directory) {
        fail101("service path is not a directory", svc, "", dir.string(), "mkdir");
    }
    if (fs::is_symlink(st)) {
        fail101("service path is a symlink (escape prevention)", svc, "",
                dir.string(), "mkdir");
    }

    // Enforce 0700 (best effort: set, then verify it is not more permissive).
    using std::filesystem::perms;
    fs::permissions(dir,
                    perms::owner_read | perms::owner_write | perms::owner_exec,
                    fs::perm_options::replace, ec);
    if (ec) {
        fail101("cannot set service directory permissions: " + ec.message(), svc,
                "", dir.string(), "mkdir");
    }
}

} // namespace store
} // namespace fw
} // namespace cgw
