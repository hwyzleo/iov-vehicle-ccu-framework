#pragma once

#include "config_node.h"
#include "config_path_resolver.h"
#include <vector>
#include <utility>

namespace ccu {
namespace fw {
namespace config {

// Deep merge of config node trees.
// - map: recursive merge (insertion order preserved from base, new keys appended).
// - scalar/sequence: later layer entirely overrides earlier.
// - explicit null: kept as an override value (distinct from missing key).
// - type conflict: later layer entirely overrides (no implicit conversion).
class Merger {
public:
    // Merge `overlay` onto `base`, returning a new immutable node.
    static NodePtr merge(NodePtr base, NodePtr overlay);

    // Merge all layers in order (later overrides earlier).
    static NodePtr
    mergeAll(const std::vector<std::pair<ConfigEntry, NodePtr>>& layers);
};

} // namespace config
} // namespace fw
} // namespace ccu
