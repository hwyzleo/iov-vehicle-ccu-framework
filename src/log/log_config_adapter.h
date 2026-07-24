#pragma once

#include "log_types.h"
#include <string>
#include <utility>

namespace cgw {
namespace fw {
namespace log {

class LogConfigAdapter {
public:
    // 从 YAML 文件读取 LogConfig
    static std::pair<LogConfig, LogErrorInfo> loadFromYaml(
        const std::string& commonConfigPath,
        const std::string& serviceConfigPath = ""
    );

    // 从 YAML 字符串读取（用于测试）
    static std::pair<LogConfig, LogErrorInfo> loadFromYamlString(
        const std::string& commonYaml,
        const std::string& serviceYaml = ""
    );

    // 校验 LogConfig 合法性
    static LogErrorInfo validate(const LogConfig& config);

    // 获取默认降级配置（console + INFO）
    static LogConfig getDefaultConfig();

private:
    static LogLevel parseLevel(const std::string& str);
};

} // namespace log
} // namespace fw
} // namespace cgw
