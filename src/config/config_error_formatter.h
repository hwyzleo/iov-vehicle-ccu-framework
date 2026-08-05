#pragma once

#include <string>
#include <string_view>

namespace cgw {
namespace fw {
namespace config {

// Builds minimal, non-sensitive error messages for diagnostics.
// Output format: "CGW-FW-000X: <reason> [path=<path> source=<source> key=<key>]"
// Empty fields are omitted. SHALL NOT include YAML content, tokens, certs or keys.
class ConfigErrorFormatter {
public:
    static std::string format(std::string_view code,
                              std::string_view reason,
                              std::string_view path,
                              std::string_view source,
                              std::string_view key);
};

} // namespace config
} // namespace fw
} // namespace cgw
