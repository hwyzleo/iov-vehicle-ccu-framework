#include "log_types.h"
#include <algorithm>
#include <cctype>

namespace ccu {
namespace fw {
namespace log {

FieldValue FieldValue::makeString(const std::string& v) {
    FieldValue fv;
    fv.type = FieldValueType::kString;
    fv.stringVal = v;
    return fv;
}

FieldValue FieldValue::makeInt(int64_t v) {
    FieldValue fv;
    fv.type = FieldValueType::kInt64;
    fv.intVal = v;
    return fv;
}

FieldValue FieldValue::makeDouble(double v) {
    FieldValue fv;
    fv.type = FieldValueType::kDouble;
    fv.doubleVal = v;
    return fv;
}

FieldValue FieldValue::makeBool(bool v) {
    FieldValue fv;
    fv.type = FieldValueType::kBool;
    fv.boolVal = v;
    return fv;
}

LogLevel logLevelFromString(const std::string& str) {
    std::string upper;
    upper.reserve(str.size());
    for (char c : str) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    if (upper == "TRACE") return LogLevel::kTrace;
    if (upper == "DEBUG") return LogLevel::kDebug;
    if (upper == "INFO")  return LogLevel::kInfo;
    if (upper == "WARN")  return LogLevel::kWarn;
    if (upper == "ERROR") return LogLevel::kError;
    if (upper == "FATAL") return LogLevel::kFatal;
    if (upper == "OFF")   return LogLevel::kOff;
    return LogLevel::kInfo; // 默认 INFO
}

const char* logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::kTrace: return "TRACE";
        case LogLevel::kDebug: return "DEBUG";
        case LogLevel::kInfo:  return "INFO";
        case LogLevel::kWarn:  return "WARN";
        case LogLevel::kError: return "ERROR";
        case LogLevel::kFatal: return "FATAL";
        case LogLevel::kOff:   return "OFF";
        default:               return "INFO";
    }
}

} // namespace log
} // namespace fw
} // namespace ccu
