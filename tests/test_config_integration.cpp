#include "config.h"
#include "config_types.h"
#include "log.h"
#include "log_types.h"
#include "config_test_util.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <string>

using namespace ccu::fw::config;
using ccu::fw::log::LogConfig;
using ccu::fw::log::LogLevel;
using ccu::fw::log::logLevelFromString;
using ccu::fw::log::Logger;
using ccu::fw::log::InitResult;
using ccu::fw::log::LogError;
using ccu::fw::log::FieldValue;
using ccutest::TempDir;

// Build a LogConfig from a config snapshot (host-service injection, US-107).
static LogConfig buildLogConfig(const ConfigSnapshot& snap) {
    LogConfig lc;
    lc.schema_version = static_cast<uint32_t>(snap.getOr<int64_t>("common.log.schema_version", 1));
    lc.level = logLevelFromString(snap.getOr<std::string>("common.log.level", "INFO"));
    lc.strict = snap.getOr<bool>("common.log.strict", false);
    lc.async_config.enabled = snap.getOr<bool>("common.log.async.enabled", true);
    lc.async_config.queue_size =
        static_cast<uint32_t>(snap.getOr<int64_t>("common.log.async.queue_size", 4096));
    lc.async_config.flush_interval_ms =
        static_cast<uint32_t>(snap.getOr<int64_t>("common.log.async.flush_interval_ms", 1000));
    lc.console_config.enabled = snap.getOr<bool>("common.log.console.enabled", true);
    lc.file_config.enabled = snap.getOr<bool>("common.log.file.enabled", false);
    lc.redact_config.raw_payload_max_bytes =
        static_cast<uint32_t>(snap.getOr<int64_t>("common.log.redact.payload_max_bytes", 256));

    if (snap.has("diag.log.modules")) {
        for (const auto& m : snap.keys("diag.log.modules")) {
            lc.module_levels[m] =
                logLevelFromString(snap.get<std::string>("diag.log.modules." + m));
        }
    }
    return lc;
}

// US-107: config snapshot injected into ccu-framework-log.
void test_config_to_log_injection() {
    TempDir root;
    root.writeFile("common.yaml",
        "common:\n  log:\n    schema_version: 1\n    level: INFO\n    strict: false\n"
        "    async:\n      enabled: true\n      queue_size: 2048\n      flush_interval_ms: 200\n"
        "    console:\n      enabled: true\n"
        "    file:\n      enabled: false\n"
        "    redact:\n      payload_max_bytes: 256\n"
        "diag:\n  log:\n    modules:\n      router: WARN\n      tester: INFO\n");

    auto snap = Config::load("diag", LoadOptions{{root.path}, TempDir().path});
    LogConfig lc = buildLogConfig(*snap);

    assert(lc.schema_version == 1);
    assert(lc.level == LogLevel::kInfo);
    assert(lc.async_config.queue_size == 2048);
    assert(lc.module_levels["router"] == LogLevel::kWarn);
    assert(lc.module_levels["tester"] == LogLevel::kInfo);

    InitResult result = Logger::init("ccu-diag", lc);
    assert(result.error == LogError::kOk);

    Logger logger = Logger::get("tester");
    logger.info("diag.uds.request", "UDS request started", {
        {"did", FieldValue::makeString("0x22")},
        {"ecu", FieldValue::makeString("BMS")}
    });
    logger.flush();

    std::cout << "  [PASS] test_config_to_log_injection" << std::endl;
}

// US-106: immutable snapshot, concurrent reads are consistent and race-free.
void test_concurrent_reads() {
    TempDir root;
    root.writeFile("common.yaml",
        "common:\n  log:\n    schema_version: 1\n    level: INFO\n"
        "    async:\n      queue_size: 4096\n"
        "diag:\n  ecu: BMS\n  ports: [8080, 9090]\n");
    auto snap = Config::load("diag", LoadOptions{{root.path}, TempDir().path});

    const int kThreads = 8;
    const int kIters = 2000;
    std::atomic<int> errors{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < kIters; ++i) {
                if (snap->get<std::string>("common.log.level") != "INFO") errors++;
                if (snap->get<int64_t>("common.log.async.queue_size") != 4096) errors++;
                if (!snap->has("diag.ecu")) errors++;
                auto v = snap->get<std::vector<int64_t>>("diag.ports");
                if (v.size() != 2 || v[0] != 8080 || v[1] != 9090) errors++;
            }
        });
    }
    for (auto& th : threads) th.join();
    assert(errors == 0);
    std::cout << "  [PASS] test_concurrent_reads" << std::endl;
}

// Acceptance: DIAG / FOTA / SEC minimal configs load successfully.
void test_minimal_service_configs() {
    const char* svcs[] = {"diag", "fota", "sec"};
    const char* svcYaml[] = {
        "diag:\n  ecu: BMS\n",
        "fota:\n  report_id: rpt-001\n",
        "sec:\n  operation: read\n"
    };
    for (int i = 0; i < 3; ++i) {
        TempDir root;
        root.writeFile("common.yaml", "common:\n  log:\n    schema_version: 1\n    level: INFO\n");
        root.writeFile("conf.d/" + std::string(svcs[i]) + ".yaml", svcYaml[i]);
        auto snap = Config::load(svcs[i], LoadOptions{{root.path}, TempDir().path});
        assert(snap != nullptr);
    }
    std::cout << "  [PASS] test_minimal_service_configs" << std::endl;
}

int main() {
    std::cout << "Running config integration tests..." << std::endl;
    test_config_to_log_injection();
    test_concurrent_reads();
    test_minimal_service_configs();
    std::cout << "All config integration tests passed!" << std::endl;
    return 0;
}
