#include "log_config_adapter.h"
#include <yaml-cpp/yaml.h>

namespace cgw {
namespace fw {
namespace log {

std::pair<LogConfig, LogErrorInfo> LogConfigAdapter::loadFromYaml(
    const std::string& commonConfigPath,
    const std::string& serviceConfigPath
) {
    try {
        YAML::Node common = YAML::LoadFile(commonConfigPath);
        YAML::Node service;
        if (!serviceConfigPath.empty()) {
            try {
                service = YAML::LoadFile(serviceConfigPath);
            } catch (const YAML::Exception&) {
                // 服务配置不存在是正常的
            }
        }
        return loadFromYamlString(YAML::Dump(common), serviceConfigPath.empty() ? "" : YAML::Dump(service));
    } catch (const YAML::Exception& e) {
        LogConfig config;
        LogErrorInfo error{LogError::kConfigInvalid, "Failed to parse YAML: " + std::string(e.what()), commonConfigPath};
        return {config, error};
    }
}

std::pair<LogConfig, LogErrorInfo> LogConfigAdapter::loadFromYamlString(
    const std::string& commonYaml,
    const std::string& serviceYaml
) {
    LogConfig config;
    LogErrorInfo error{LogError::kOk, "", ""};

    try {
        YAML::Node common = YAML::Load(commonYaml);
        YAML::Node service = serviceYaml.empty() ? YAML::Node() : YAML::Load(serviceYaml);

        if (common["common"] && common["common"]["log"]) {
            YAML::Node logNode = common["common"]["log"];

            if (logNode["schema_version"]) {
                config.schema_version = logNode["schema_version"].as<uint32_t>(1);
            }
            if (logNode["level"]) {
                config.level = parseLevel(logNode["level"].as<std::string>("INFO"));
            }
            if (logNode["strict"]) {
                config.strict = logNode["strict"].as<bool>(false);
            }

            if (logNode["async"]) {
                YAML::Node asyncNode = logNode["async"];
                if (asyncNode["enabled"]) config.async_config.enabled = asyncNode["enabled"].as<bool>(true);
                if (asyncNode["queue_size"]) config.async_config.queue_size = asyncNode["queue_size"].as<uint32_t>(4096);
                if (asyncNode["flush_interval_ms"]) config.async_config.flush_interval_ms = asyncNode["flush_interval_ms"].as<uint32_t>(1000);
            }

            if (logNode["console"]) {
                YAML::Node consoleNode = logNode["console"];
                if (consoleNode["enabled"]) config.console_config.enabled = consoleNode["enabled"].as<bool>(true);
            }

            if (logNode["file"]) {
                YAML::Node fileNode = logNode["file"];
                if (fileNode["enabled"]) config.file_config.enabled = fileNode["enabled"].as<bool>(false);
                if (fileNode["root"]) config.file_config.root = fileNode["root"].as<std::string>("/var/log/tbox");
                if (fileNode["max_file_size_mb"]) config.file_config.max_file_size_mb = fileNode["max_file_size_mb"].as<uint32_t>(20);
                if (fileNode["max_files"]) config.file_config.max_files = fileNode["max_files"].as<uint32_t>(5);
                if (fileNode["total_budget_mb"]) config.file_config.total_budget_mb = fileNode["total_budget_mb"].as<uint32_t>(100);
            }

            if (logNode["redact"]) {
                YAML::Node redactNode = logNode["redact"];
                if (redactNode["identifiers"]) config.redact_config.identifiers = redactNode["identifiers"].as<std::string>("mask");
                if (redactNode["raw_payload_max_bytes"]) config.redact_config.raw_payload_max_bytes = redactNode["raw_payload_max_bytes"].as<uint32_t>(256);
            }
        }

        // 服务级覆盖
        if (service && service["log"]) {
            YAML::Node svcLog = service["log"];
            if (svcLog["level"]) {
                config.level = parseLevel(svcLog["level"].as<std::string>("INFO"));
            }
            if (svcLog["modules"]) {
                YAML::Node modules = svcLog["modules"];
                for (auto it = modules.begin(); it != modules.end(); ++it) {
                    std::string moduleName = it->first.as<std::string>();
                    config.module_levels[moduleName] = parseLevel(it->second.as<std::string>("INFO"));
                }
            }
        }

        LogErrorInfo validationError = validate(config);
        if (validationError.code != LogError::kOk) {
            return {getDefaultConfig(), validationError};
        }

        return {config, error};

    } catch (const YAML::Exception& e) {
        return {getDefaultConfig(), {LogError::kConfigInvalid, "YAML parse error: " + std::string(e.what()), ""}};
    }
}

LogErrorInfo LogConfigAdapter::validate(const LogConfig& config) {
    if (config.schema_version != 1) {
        return {LogError::kConfigInvalid, "schema_version must be 1, got " + std::to_string(config.schema_version), ""};
    }

    if (config.async_config.queue_size == 0) {
        return {LogError::kConfigInvalid, "async.queue_size must be positive", ""};
    }

    if (config.file_config.enabled && config.file_config.root.empty()) {
        return {LogError::kConfigInvalid, "file.root is required when file sink is enabled", ""};
    }

    if (config.file_config.enabled) {
        uint64_t totalNeeded = static_cast<uint64_t>(config.file_config.max_file_size_mb) * config.file_config.max_files;
        if (totalNeeded > config.file_config.total_budget_mb) {
            return {LogError::kConfigInvalid,
                    "max_file_size_mb × max_files (" + std::to_string(totalNeeded) +
                    ") exceeds total_budget_mb (" + std::to_string(config.file_config.total_budget_mb) + ")",
                    ""};
        }
    }

    return {LogError::kOk, "", ""};
}

LogConfig LogConfigAdapter::getDefaultConfig() {
    LogConfig config;
    config.schema_version = 1;
    config.level = LogLevel::kInfo;
    config.strict = false;
    config.console_config.enabled = true;
    config.file_config.enabled = false;
    return config;
}

LogLevel LogConfigAdapter::parseLevel(const std::string& str) {
    return logLevelFromString(str);
}

} // namespace log
} // namespace fw
} // namespace cgw
