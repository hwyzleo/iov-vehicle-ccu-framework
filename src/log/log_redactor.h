#pragma once

#include "log_types.h"
#include <vector>
#include <string>
#include <unordered_set>

namespace ccu {
namespace fw {
namespace log {

class Redactor {
public:
    explicit Redactor(const RedactConfig& config);

    // 对字段列表执行脱敏
    std::vector<Field> redact(std::vector<Field> fields) const;

private:
    RedactConfig m_config;
    static const std::unordered_set<std::string> s_secretKeys;

    bool isSecretKey(const std::string& key) const;
    std::string maskValue(const std::string& value) const;
    std::string truncatePayload(const std::string& value) const;
    std::string hashValue(const std::string& value) const;
};

} // namespace log
} // namespace fw
} // namespace ccu
