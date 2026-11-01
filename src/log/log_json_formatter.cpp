#include "log_json_formatter.h"
#include <sstream>
#include <cstdio>

namespace ccu {
namespace fw {
namespace log {

std::string JsonLineFormatter::format(const std::vector<Field>& fields) {
    std::ostringstream oss;
    oss << '{';
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) oss << ',';
        oss << '"' << escapeString(fields[i].key) << "\":" << formatValue(fields[i].value);
    }
    oss << '}';
    return oss.str();
}

std::string JsonLineFormatter::escapeString(const std::string& str) {
    std::string result;
    result.reserve(str.size() + 8);
    for (char c : str) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b";  break;
            case '\f': result += "\\f";  break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    result += buf;
                } else {
                    result += c;
                }
                break;
        }
    }
    return result;
}

std::string JsonLineFormatter::formatValue(const FieldValue& value) {
    switch (value.type) {
        case FieldValueType::kString:
            return '"' + escapeString(value.stringVal) + '"';
        case FieldValueType::kInt64:
            return std::to_string(value.intVal);
        case FieldValueType::kDouble: {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", value.doubleVal);
            return std::string(buf);
        }
        case FieldValueType::kBool:
            return value.boolVal ? "true" : "false";
        default:
            return "null";
    }
}

} // namespace log
} // namespace fw
} // namespace ccu
