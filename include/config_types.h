#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <cstdint>

namespace ccu {
namespace fw {
namespace config {

// ============================================================
// ConfigException (CCU-FW-0001~0004)
// ============================================================
// code: CCU-FW-0001 (file missing/unreadable/read failed)
//       CCU-FW-0002 (YAML syntax/parse error)
//       CCU-FW-0003 (required key missing)
//       CCU-FW-0004 (node type/enum/range illegal)
// path: normalized source file path (empty for non-file errors)
// key:  offending config key or input name (e.g. "service", "configRoots")
struct ConfigException : std::runtime_error {
    std::string code;
    std::string path;
    std::string key;

    ConfigException(std::string code_, const std::string& message,
                    std::string path_ = "", std::string key_ = "")
        : std::runtime_error(message)
        , code(std::move(code_))
        , path(std::move(path_))
        , key(std::move(key_)) {}
};

// ============================================================
// ConfigSnapshot - immutable read-only view of the merged config
// ============================================================
// Public header does NOT expose the internal YAML/node tree. Typed access is
// provided through template getters that dispatch to hidden implementation.
// Supported T: std::string, int64_t, double, bool,
//              std::vector<std::string>, std::vector<int64_t>
namespace detail {
class SnapshotImpl;

std::string              snapshotGetString(const SnapshotImpl& impl, std::string_view key);
int64_t                  snapshotGetInt64(const SnapshotImpl& impl, std::string_view key);
double                   snapshotGetDouble(const SnapshotImpl& impl, std::string_view key);
bool                     snapshotGetBool(const SnapshotImpl& impl, std::string_view key);
std::vector<std::string> snapshotGetStringVec(const SnapshotImpl& impl, std::string_view key);
std::vector<int64_t>     snapshotGetInt64Vec(const SnapshotImpl& impl, std::string_view key);
bool                     snapshotHas(const SnapshotImpl& impl, std::string_view key);
std::vector<std::string> snapshotKeys(const SnapshotImpl& impl, std::string_view section);

template <class T> struct SnapshotGetter;
template <> struct SnapshotGetter<std::string> {
    static std::string get(const SnapshotImpl& i, std::string_view k) { return snapshotGetString(i, k); }
};
template <> struct SnapshotGetter<int64_t> {
    static int64_t get(const SnapshotImpl& i, std::string_view k) { return snapshotGetInt64(i, k); }
};
template <> struct SnapshotGetter<double> {
    static double get(const SnapshotImpl& i, std::string_view k) { return snapshotGetDouble(i, k); }
};
template <> struct SnapshotGetter<bool> {
    static bool get(const SnapshotImpl& i, std::string_view k) { return snapshotGetBool(i, k); }
};
template <> struct SnapshotGetter<std::vector<std::string>> {
    static std::vector<std::string> get(const SnapshotImpl& i, std::string_view k) { return snapshotGetStringVec(i, k); }
};
template <> struct SnapshotGetter<std::vector<int64_t>> {
    static std::vector<int64_t> get(const SnapshotImpl& i, std::string_view k) { return snapshotGetInt64Vec(i, k); }
};
} // namespace detail

class ConfigSnapshot {
public:
    // Typed read. Throws ConfigException(CCU-FW-0003) if key missing,
    // (CCU-FW-0004) on type mismatch / unparseable value.
    template <class T>
    T get(std::string_view key) const {
        return detail::SnapshotGetter<T>::get(*m_impl, key);
    }

    // Returns defaultValue only when key is missing; throws on type mismatch.
    template <class T>
    T getOr(std::string_view key, T defaultValue) const {
        return detail::snapshotHas(*m_impl, key) ? get<T>(key) : defaultValue;
    }

    // Convenience overload for string-literal defaults.
    std::string getOr(std::string_view key, const char* defaultValue) const {
        return detail::snapshotHas(*m_impl, key)
                   ? get<std::string>(key)
                   : std::string(defaultValue);
    }

    // Explicit null counts as present (distinguishes from missing key).
    bool has(std::string_view key) const {
        return detail::snapshotHas(*m_impl, key);
    }

    // Immediate child keys of the node at `section` (dotted path).
    // Empty for missing section or non-map node.
    std::vector<std::string> keys(std::string_view section) const {
        return detail::snapshotKeys(*m_impl, section);
    }

private:
    std::shared_ptr<const detail::SnapshotImpl> m_impl;

    explicit ConfigSnapshot(std::shared_ptr<const detail::SnapshotImpl> impl)
        : m_impl(std::move(impl)) {}

    friend class Config;
};

} // namespace config
} // namespace fw
} // namespace ccu
