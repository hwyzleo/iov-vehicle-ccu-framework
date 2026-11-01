#pragma once

#include "config.h"

namespace ccu {
namespace fw {
namespace config {

// Built-in validator for common.log.* fields (per CCU-FW-SPEC设计 §4).
// Runs automatically on every Config::load when common.log is present.
class CommonLogValidator : public Validator {
public:
    void validate(const ConfigSnapshot& snapshot) const override;
};

} // namespace config
} // namespace fw
} // namespace ccu
