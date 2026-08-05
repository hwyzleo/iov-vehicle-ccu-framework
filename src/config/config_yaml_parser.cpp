#include "config_yaml_parser.h"
#include <yaml-cpp/yaml.h>

namespace cgw {
namespace fw {
namespace config {

namespace {

NodePtr convert(const YAML::Node& n) {
    if (!n.IsDefined() || n.IsNull()) {
        return ConfigNode::makeNull();
    }
    if (n.IsScalar()) {
        return ConfigNode::makeScalar(n.Scalar());
    }
    if (n.IsSequence()) {
        auto node = ConfigNode::makeSequence();
        node->sequence.reserve(n.size());
        for (const auto& e : n) {
            node->sequence.push_back(convert(e));
        }
        return node;
    }
    if (n.IsMap()) {
        auto node = ConfigNode::makeMap();
        node->map.reserve(n.size());
        for (const auto& kv : n) {
            node->map.emplace_back(kv.first.Scalar(), convert(kv.second));
        }
        return node;
    }
    return ConfigNode::makeNull();
}

} // namespace

NodePtr YamlParser::parse(const std::string& text,
                          const std::filesystem::path& sourcePath) {
    YAML::Node root;
    try {
        root = YAML::Load(text);
    } catch (const YAML::Exception& e) {
        throw ConfigException(
            "CGW-FW-0002",
            "YAML parse error: " + std::string(e.what()),
            sourcePath.string(),
            "");
    }

    // Empty / null root -> empty map (no keys, no override).
    if (!root.IsDefined() || root.IsNull()) {
        return ConfigNode::makeMap();
    }
    return convert(root);
}

} // namespace config
} // namespace fw
} // namespace cgw
