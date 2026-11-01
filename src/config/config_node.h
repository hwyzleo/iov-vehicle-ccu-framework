#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <cstdint>

namespace ccu {
namespace fw {
namespace config {

// ============================================================
// Internal node tree (NOT exposed via public headers)
// ============================================================
enum class NodeType : uint8_t {
    kNull,
    kScalar,
    kMap,
    kSequence
};

struct ConfigNode;
using NodePtr = std::shared_ptr<const ConfigNode>;
// Mutable handle used only during construction.
using MutableNodePtr = std::shared_ptr<ConfigNode>;

struct ConfigNode {
    NodeType type = NodeType::kNull;
    std::string scalar;                                        // kScalar (raw YAML text)
    std::vector<std::pair<std::string, NodePtr>> map;          // kMap (insertion-ordered)
    std::vector<NodePtr> sequence;                             // kSequence

    static MutableNodePtr makeNull();
    static MutableNodePtr makeScalar(std::string s);
    static MutableNodePtr makeMap();
    static MutableNodePtr makeSequence();

    // Map lookup by key; returns nullptr if not a map or key absent.
    const ConfigNode* find(std::string_view key) const;
};

} // namespace config
} // namespace fw
} // namespace ccu
