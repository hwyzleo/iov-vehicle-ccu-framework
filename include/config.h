#pragma once

#include "config_types.h"
#include <filesystem>
#include <vector>
#include <memory>
#include <string>
#include <string_view>

namespace ccu {
namespace fw {
namespace config {

// ============================================================
// LoadOptions
// ============================================================
// configRoots: ordered configuration root directories (production default
//              at least /etc/ccu). Duplicate roots are de-duplicated keeping
//              first occurrence order. Roots are lexically normalized before
//              lookup; empty/illegal roots are rejected at init.
// cwd:         stable working directory used for ./config/* and ./<svc>.yaml.
//              systemd services SHALL set an explicit WorkingDirectory.
struct LoadOptions {
    std::vector<std::filesystem::path> configRoots{"/etc/ccu"};
    std::filesystem::path cwd{"."};
};

// ============================================================
// Validator - pluggable pre-publish validation
// ============================================================
class Validator {
public:
    virtual ~Validator() = default;
    // Throw ConfigException (CCU-FW-0003 / CCU-FW-0004) on failure.
    virtual void validate(const ConfigSnapshot& snapshot) const = 0;
};

// ============================================================
// Config facade
// ============================================================
class Config {
public:
    // Load, merge and validate configuration, returning an immutable snapshot.
    // Throws ConfigException on any failure (no partial config is published).
    static std::shared_ptr<const ConfigSnapshot>
    load(std::string_view service, const LoadOptions& options = {});

    // Register a service-specific Validator; runs on load(service, ...).
    // The built-in common.log.* Validator always runs automatically.
    static void registerValidator(std::string_view service,
                                  std::shared_ptr<Validator> validator);
};

} // namespace config
} // namespace fw
} // namespace ccu
