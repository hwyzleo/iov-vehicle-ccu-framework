#include "config_merger.h"
#include <algorithm>

namespace cgw {
namespace fw {
namespace config {

NodePtr Merger::merge(NodePtr base, NodePtr overlay) {
    if (!overlay) return base;
    // Explicit null overrides everything (kept as a distinct override value).
    if (overlay->type == NodeType::kNull) return overlay;
    if (!base || base->type == NodeType::kNull) return overlay;

    if (base->type == NodeType::kMap && overlay->type == NodeType::kMap) {
        auto result = ConfigNode::makeMap();
        result->map.reserve(base->map.size() + overlay->map.size());
        // Copy base entries (share immutable children).
        for (const auto& kv : base->map) {
            result->map.emplace_back(kv.first, kv.second);
        }
        // Apply overlay entries.
        for (const auto& kv : overlay->map) {
            auto it = std::find_if(result->map.begin(), result->map.end(),
                                   [&](const std::pair<std::string, NodePtr>& p) {
                                       return p.first == kv.first;
                                   });
            if (it != result->map.end()) {
                it->second = merge(it->second, kv.second);
            } else {
                result->map.emplace_back(kv.first, kv.second);
            }
        }
        return result;
    }

    // scalar/sequence or type conflict: overlay entirely replaces base.
    return overlay;
}

NodePtr Merger::mergeAll(const std::vector<std::pair<ConfigEntry, NodePtr>>& layers) {
    NodePtr acc = ConfigNode::makeMap(); // empty map as starting point
    for (const auto& layer : layers) {
        acc = merge(acc, layer.second);
    }
    return acc;
}

} // namespace config
} // namespace fw
} // namespace cgw
