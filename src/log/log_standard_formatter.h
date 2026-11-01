#pragma once

#include "log_types.h"
#include <string>
#include <vector>

namespace ccu {
namespace fw {
namespace log {

class StandardFormatter {
public:
    // 将字段列表格式化为标准日志行
    // 格式：[YYYY-MM-DD HH:MM:SS.mmm] [service] [level] message[, key=value ...]
    static std::string format(const std::vector<Field>& fields);

private:
    // 从 fields 中提取关键字段值
    static std::string extractFieldValue(const std::vector<Field>& fields,
                                         const std::string& key,
                                         const std::string& defaultValue = "");

    // UTC ISO 时间戳转本地时间格式
    // 输入：2026-07-26T03:55:56.848Z
    // 输出：2026-07-26 11:55:56.848
    static std::string utcToLocalFormat(const std::string& utcTimestamp);

    // 格式化附加字段（排除公共字段）
    static std::string formatExtraFields(const std::vector<Field>& fields);

    // 判断是否为公共字段（应排除）
    static bool isCommonField(const std::string& key);
};

} // namespace log
} // namespace fw
} // namespace ccu
