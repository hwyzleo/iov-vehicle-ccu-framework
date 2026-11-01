#include "log_redactor.h"
#include <algorithm>
#include <cctype>

namespace ccu {
namespace fw {
namespace log {

const std::unordered_set<std::string> Redactor::s_secretKeys = {
    "password", "passwd", "token", "secret", "private_key",
    "seed", "key_material", "access_key", "secret_key"
};

Redactor::Redactor(const RedactConfig& config) : m_config(config) {}

std::vector<Field> Redactor::redact(std::vector<Field> fields) const {
    std::vector<Field> result;
    result.reserve(fields.size());

    for (auto& field : fields) {
        Sensitivity effectiveSensitivity = field.sensitivity;
        if (isSecretKey(field.key)) {
            effectiveSensitivity = Sensitivity::Secret;
        }

        if (effectiveSensitivity == Sensitivity::Secret) {
            result.push_back({
                field.key + "_redacted",
                FieldValue::makeString("[REDACTED:secret]")
            });
            continue;
        }

        if (field.value.type == FieldValueType::kString) {
            switch (effectiveSensitivity) {
                case Sensitivity::Identifier: {
                    if (m_config.identifiers == "mask") {
                        Field masked = field;
                        masked.value = FieldValue::makeString(maskValue(field.value.stringVal));
                        result.push_back(std::move(masked));
                    } else if (m_config.identifiers == "reject") {
                        result.push_back({
                            field.key + "_redacted",
                            FieldValue::makeString("[REDACTED:identifier]")
                        });
                    } else {
                        Field hashed = field;
                        hashed.value = FieldValue::makeString(hashValue(field.value.stringVal));
                        result.push_back(std::move(hashed));
                    }
                    break;
                }
                case Sensitivity::Payload: {
                    Field truncated = field;
                    truncated.value = FieldValue::makeString(truncatePayload(field.value.stringVal));
                    result.push_back(std::move(truncated));
                    break;
                }
                default:
                    result.push_back(std::move(field));
                    break;
            }
        } else {
            result.push_back(std::move(field));
        }
    }

    return result;
}

bool Redactor::isSecretKey(const std::string& key) const {
    std::string lower;
    lower.reserve(key.size());
    for (char c : key) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return s_secretKeys.count(lower) > 0;
}

std::string Redactor::maskValue(const std::string& value) const {
    if (value.size() <= 4) {
        return "****";
    }
    return value.substr(0, 2) + "****" + value.substr(value.size() - 2);
}

std::string Redactor::truncatePayload(const std::string& value) const {
    if (value.size() <= m_config.raw_payload_max_bytes) {
        return value;
    }
    return value.substr(0, m_config.raw_payload_max_bytes) + "...[truncated]";
}

std::string Redactor::hashValue(const std::string& value) const {
    return "[hash:" + std::to_string(value.size()) + "]";
}

} // namespace log
} // namespace fw
} // namespace ccu
