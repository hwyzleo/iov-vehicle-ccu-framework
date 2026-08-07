#pragma once

#include <string>
#include <string_view>

namespace cgw {
namespace fw {
namespace store {

// Builds minimal, non-sensitive error messages for diagnostics.
// Output format: "CGW-FW-010X: <reason> [service=<svc> key=<key> path=<path> stage=<stage>]"
// Empty fields are omitted. SHALL NOT include state contents, tokens, certs or keys.
class StoreErrorFormatter {
public:
    static std::string format(std::string_view code,
                              std::string_view reason,
                              std::string_view service,
                              std::string_view key,
                              std::string_view path,
                              std::string_view stage);
};

} // namespace store
} // namespace fw
} // namespace cgw
