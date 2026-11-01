#include "log_types.h"
#include "log/log_level_filter.h"
#include <cassert>
#include <iostream>

using namespace ccu::fw::log;

void test_global_level_filter() {
    LogConfig config;
    config.level = LogLevel::kWarn;
    LevelFilter filter(config);

    assert(filter.shouldLog(LogLevel::kTrace, "any") == false);
    assert(filter.shouldLog(LogLevel::kDebug, "any") == false);
    assert(filter.shouldLog(LogLevel::kInfo, "any") == false);
    assert(filter.shouldLog(LogLevel::kWarn, "any") == true);
    assert(filter.shouldLog(LogLevel::kError, "any") == true);
    assert(filter.shouldLog(LogLevel::kFatal, "any") == true);

    std::cout << "  [PASS] test_global_level_filter" << std::endl;
}

void test_module_override() {
    LogConfig config;
    config.level = LogLevel::kInfo;
    config.module_levels["transport"] = LogLevel::kWarn;
    config.module_levels["uds"] = LogLevel::kDebug;

    LevelFilter filter(config);

    assert(filter.shouldLog(LogLevel::kInfo, "transport") == false);
    assert(filter.shouldLog(LogLevel::kWarn, "transport") == true);

    assert(filter.shouldLog(LogLevel::kDebug, "uds") == true);
    assert(filter.shouldLog(LogLevel::kTrace, "uds") == false);

    assert(filter.shouldLog(LogLevel::kInfo, "other") == true);
    assert(filter.shouldLog(LogLevel::kDebug, "other") == false);

    std::cout << "  [PASS] test_module_override" << std::endl;
}

void test_dynamic_level_update() {
    LogConfig config;
    config.level = LogLevel::kInfo;
    LevelFilter filter(config);

    assert(filter.shouldLog(LogLevel::kDebug, "any") == false);

    filter.setGlobalLevel(LogLevel::kDebug);
    assert(filter.shouldLog(LogLevel::kDebug, "any") == true);

    filter.setModuleLevel("my_module", LogLevel::kError);
    assert(filter.shouldLog(LogLevel::kWarn, "my_module") == false);
    assert(filter.shouldLog(LogLevel::kError, "my_module") == true);

    std::cout << "  [PASS] test_dynamic_level_update" << std::endl;
}

int main() {
    std::cout << "Running LevelFilter tests..." << std::endl;
    test_global_level_filter();
    test_module_override();
    test_dynamic_level_update();
    std::cout << "All LevelFilter tests passed!" << std::endl;
    return 0;
}
