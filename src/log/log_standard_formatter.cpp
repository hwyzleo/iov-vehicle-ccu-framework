#include "log_standard_formatter.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstring>

namespace cgw {
namespace fw {
namespace log {

// 公共字段列表（不输出到附加字段）
static const std::vector<std::string> kCommonFields = {
    "schema_version", "timestamp", "time_synced", "mono_ms",
    "level", "service", "module", "event", "message",
    "pid", "tid", "trace_id", "request_id", "session_id"
};

std::string StandardFormatter::format(const std::vector<Field>& fields) {
    std::ostringstream oss;

    // 提取关键字段
    std::string timestamp = extractFieldValue(fields, "timestamp", "----/--/-- --:--:--.---");
    std::string service = extractFieldValue(fields, "service", "unknown");
    std::string level = extractFieldValue(fields, "level", "info");
    std::string message = extractFieldValue(fields, "message", "");

    // 转换时间格式
    std::string localTime = utcToLocalFormat(timestamp);

    // 转换级别为小写
    std::string levelLower;
    levelLower.reserve(level.size());
    for (char c : level) {
        levelLower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    // 输出主格式
    oss << "[" << localTime << "] ";
    oss << "[" << service << "] ";
    oss << "[" << levelLower << "] ";
    oss << message;

    // 输出附加字段
    std::string extra = formatExtraFields(fields);
    if (!extra.empty()) {
        oss << ", " << extra;
    }

    return oss.str();
}

std::string StandardFormatter::extractFieldValue(const std::vector<Field>& fields,
                                                  const std::string& key,
                                                  const std::string& defaultValue) {
    for (const auto& field : fields) {
        if (field.key == key && field.value.type == FieldValueType::kString) {
            return field.value.stringVal;
        }
    }
    return defaultValue;
}

std::string StandardFormatter::utcToLocalFormat(const std::string& utcTimestamp) {
    // 解析 ISO 格式：2026-07-26T03:55:56.848Z
    if (utcTimestamp.size() < 20 || utcTimestamp[10] != 'T') {
        return utcTimestamp;  // 格式不匹配，原样返回
    }

    struct tm tmUtc;
    memset(&tmUtc, 0, sizeof(tmUtc));

    // 解析日期时间部分
    if (sscanf(utcTimestamp.c_str(), "%d-%d-%dT%d:%d:%d",
               &tmUtc.tm_year, &tmUtc.tm_mon, &tmUtc.tm_mday,
               &tmUtc.tm_hour, &tmUtc.tm_min, &tmUtc.tm_sec) != 6) {
        return utcTimestamp;
    }

    tmUtc.tm_year -= 1900;  // tm_year 从 1900 开始
    tmUtc.tm_mon -= 1;      // tm_mon 从 0 开始

    // 提取毫秒
    int ms = 0;
    size_t dotPos = utcTimestamp.find('.', 19);
    if (dotPos != std::string::npos && dotPos + 3 < utcTimestamp.size()) {
        ms = std::atoi(utcTimestamp.substr(dotPos + 1, 3).c_str());
    }

    // UTC 转本地时间
    time_t utcTime = timegm(&tmUtc);
    struct tm tmLocal;
    localtime_r(&utcTime, &tmLocal);

    // 格式化输出
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tmLocal.tm_year + 1900, tmLocal.tm_mon + 1, tmLocal.tm_mday,
             tmLocal.tm_hour, tmLocal.tm_min, tmLocal.tm_sec, ms);

    return std::string(buf);
}

std::string StandardFormatter::formatExtraFields(const std::vector<Field>& fields) {
    std::ostringstream oss;
    bool first = true;

    for (const auto& field : fields) {
        if (isCommonField(field.key)) {
            continue;
        }

        if (!first) {
            oss << ", ";
        }
        first = false;

        oss << field.key << "=";

        switch (field.value.type) {
            case FieldValueType::kString:
                oss << field.value.stringVal;
                break;
            case FieldValueType::kInt64:
                oss << field.value.intVal;
                break;
            case FieldValueType::kDouble:
                oss << field.value.doubleVal;
                break;
            case FieldValueType::kBool:
                oss << (field.value.boolVal ? "true" : "false");
                break;
        }
    }

    return oss.str();
}

bool StandardFormatter::isCommonField(const std::string& key) {
    for (const auto& common : kCommonFields) {
        if (key == common) {
            return true;
        }
    }
    return false;
}

} // namespace log
} // namespace fw
} // namespace cgw
