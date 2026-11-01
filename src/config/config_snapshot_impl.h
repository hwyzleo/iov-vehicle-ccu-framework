#pragma once

#include "config_node.h"
#include "config_types.h"
#include <string_view>
#include <vector>
#include <string>

namespace ccu {
namespace fw {
namespace config {
namespace detail {

// Hidden implementation backing ConfigSnapshot. Holds the immutable merged
// tree and provides typed navigation/getters.
class SnapshotImpl {
public:
    explicit SnapshotImpl(NodePtr root) : m_root(std::move(root)) {}

    bool has(std::string_view key) const;
    std::vector<std::string> keys(std::string_view section) const;

    std::string              getString(std::string_view key) const;
    int64_t                  getInt64(std::string_view key) const;
    double                   getDouble(std::string_view key) const;
    bool                     getBool(std::string_view key) const;
    std::vector<std::string> getStringVec(std::string_view key) const;
    std::vector<int64_t>     getInt64Vec(std::string_view key) const;

private:
    NodePtr m_root;

    // Navigate dotted path to the leaf node; nullptr if missing or not navigable.
    const ConfigNode* lookup(std::string_view key) const;
    const ConfigNode* lookupSection(std::string_view section) const;

    static void throwMissing(std::string_view key);
    static void throwType(std::string_view key, const std::string& detail);
};

} // namespace detail
} // namespace config
} // namespace fw
} // namespace ccu
