#pragma once

#include "config.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <string_view>

namespace cgw {
namespace fw {
namespace config {

// Registry of common (built-in) and service-specific Validators.
// Common validators run on every load; service validators run on load(service).
class ValidatorRegistry {
public:
    static ValidatorRegistry& instance();

    void registerServiceValidator(std::string_view service,
                                  std::shared_ptr<Validator> validator);

    // Run common validators + validators registered for `service`.
    // Throws ConfigException on the first failure.
    void validateAll(std::string_view service, const ConfigSnapshot& snapshot);

private:
    ValidatorRegistry();

    std::mutex m_mutex;
    std::vector<std::shared_ptr<Validator>> m_common;
    std::unordered_map<std::string, std::vector<std::shared_ptr<Validator>>> m_service;
};

} // namespace config
} // namespace fw
} // namespace cgw
