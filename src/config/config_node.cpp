#include "config_node.h"

namespace cgw {
namespace fw {
namespace config {

MutableNodePtr ConfigNode::makeNull() {
    return std::make_shared<ConfigNode>();
}

MutableNodePtr ConfigNode::makeScalar(std::string s) {
    auto n = std::make_shared<ConfigNode>();
    n->type = NodeType::kScalar;
    n->scalar = std::move(s);
    return n;
}

MutableNodePtr ConfigNode::makeMap() {
    auto n = std::make_shared<ConfigNode>();
    n->type = NodeType::kMap;
    return n;
}

MutableNodePtr ConfigNode::makeSequence() {
    auto n = std::make_shared<ConfigNode>();
    n->type = NodeType::kSequence;
    return n;
}

const ConfigNode* ConfigNode::find(std::string_view key) const {
    if (type != NodeType::kMap) return nullptr;
    for (const auto& kv : map) {
        if (kv.first == key) return kv.second.get();
    }
    return nullptr;
}

} // namespace config
} // namespace fw
} // namespace cgw
