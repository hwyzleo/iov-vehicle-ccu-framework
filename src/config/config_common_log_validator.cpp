#include "config_common_log_validator.h"
#include <algorithm>
#include <cctype>
#include <string>

namespace cgw {
namespace fw {
namespace config {

namespace {

bool isLevelValid(const std::string& s) {
    static const char* kLevels[] = {
        "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    for (const char* lvl : kLevels) {
        if (s == lvl) return true;
    }
    return false;
}

} // namespace

void CommonLogValidator::validate(const ConfigSnapshot& snapshot) const {
    if (!snapshot.has("common.log")) {
        return; // no log configuration to validate
    }

    // schema_version (defaults to 1; if present must equal 1)
    if (snapshot.has("common.log.schema_version")) {
        int64_t v = snapshot.get<int64_t>("common.log.schema_version");
        if (v != 1) {
            throw ConfigException(
                "CGW-FW-0004",
                "common.log.schema_version must be 1, got " + std::to_string(v),
                "", "common.log.schema_version");
        }
    }

    // level enum
    if (snapshot.has("common.log.level")) {
        std::string lvl = snapshot.get<std::string>("common.log.level");
        if (!isLevelValid(lvl)) {
            throw ConfigException(
                "CGW-FW-0004",
                "common.log.level invalid: " + lvl,
                "", "common.log.level");
        }
    }

    // async.queue_size > 0
    if (snapshot.has("common.log.async.queue_size")) {
        int64_t qs = snapshot.get<int64_t>("common.log.async.queue_size");
        if (qs <= 0) {
            throw ConfigException(
                "CGW-FW-0004",
                "common.log.async.queue_size must be positive",
                "", "common.log.async.queue_size");
        }
    }

    // file sink budget: when enabled, root required and budget consistent.
    bool fileEnabled = false;
    if (snapshot.has("common.log.file.enabled")) {
        fileEnabled = snapshot.get<bool>("common.log.file.enabled");
    }
    if (fileEnabled) {
        if (!snapshot.has("common.log.file.root")) {
            throw ConfigException(
                "CGW-FW-0003",
                "common.log.file.root is required when file sink is enabled",
                "", "common.log.file.root");
        }
        int64_t maxSize = snapshot.has("common.log.file.max_file_size_mb")
                              ? snapshot.get<int64_t>("common.log.file.max_file_size_mb") : 20;
        int64_t maxFiles = snapshot.has("common.log.file.max_files")
                               ? snapshot.get<int64_t>("common.log.file.max_files") : 5;
        int64_t budget = snapshot.has("common.log.file.total_budget_mb")
                             ? snapshot.get<int64_t>("common.log.file.total_budget_mb") : 100;
        if (maxSize <= 0 || maxFiles <= 0 || budget <= 0) {
            throw ConfigException(
                "CGW-FW-0004",
                "common.log.file size/files/budget must be positive",
                "", "common.log.file");
        }
        if (static_cast<int64_t>(maxSize) * maxFiles > budget) {
            throw ConfigException(
                "CGW-FW-0004",
                "common.log.file max_file_size_mb * max_files (" +
                    std::to_string(maxSize * maxFiles) +
                    ") exceeds total_budget_mb (" + std::to_string(budget) + ")",
                "", "common.log.file");
        }
    }

    // redact.payload_max_bytes > 0 (shall set a limit, not unlimited)
    if (snapshot.has("common.log.redact.payload_max_bytes")) {
        int64_t pmb = snapshot.get<int64_t>("common.log.redact.payload_max_bytes");
        if (pmb <= 0) {
            throw ConfigException(
                "CGW-FW-0004",
                "common.log.redact.payload_max_bytes must be positive (not unlimited)",
                "", "common.log.redact.payload_max_bytes");
        }
    }
}

} // namespace config
} // namespace fw
} // namespace cgw
