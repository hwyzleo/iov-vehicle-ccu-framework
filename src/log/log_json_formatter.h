#pragma once

#include "log_types.h"
#include <string>
#include <vector>

namespace ccu {
namespace fw {
namespace log {

class JsonLineFormatter {
public:
    static std::string format(const std::vector<Field>& fields);

private:
    static std::string escapeString(const std::string& str);
    static std::string formatValue(const FieldValue& value);
};

} // namespace log
} // namespace fw
} // namespace ccu
