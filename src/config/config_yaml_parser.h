#pragma once

#include "config_node.h"
#include "config_types.h"
#include <string>
#include <filesystem>

namespace ccu {
namespace fw {
namespace config {

// Parses YAML text into an internal node tree.
// - Empty / null root yields an empty map (contributes no keys).
// - Throws ConfigException(CCU-FW-0002) on syntax error.
class YamlParser {
public:
    static NodePtr parse(const std::string& text,
                         const std::filesystem::path& sourcePath);
};

} // namespace config
} // namespace fw
} // namespace ccu
