#include "config_validator_registry.h"
#include "config_common_log_validator.h"

namespace ccu {
namespace fw {
namespace config {

ValidatorRegistry& ValidatorRegistry::instance() {
    static ValidatorRegistry inst;
    return inst;
}

ValidatorRegistry::ValidatorRegistry() {
    m_common.push_back(std::make_shared<CommonLogValidator>());
}

void ValidatorRegistry::registerServiceValidator(std::string_view service,
                                                 std::shared_ptr<Validator> validator) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_service[std::string(service)].push_back(std::move(validator));
}

void ValidatorRegistry::validateAll(std::string_view service,
                                    const ConfigSnapshot& snapshot) {
    std::vector<std::shared_ptr<Validator>> common;
    std::vector<std::shared_ptr<Validator>> svc;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        common = m_common;
        auto it = m_service.find(std::string(service));
        if (it != m_service.end()) svc = it->second;
    }
    for (const auto& v : common) {
        v->validate(snapshot);
    }
    for (const auto& v : svc) {
        v->validate(snapshot);
    }
}

} // namespace config
} // namespace fw
} // namespace ccu
