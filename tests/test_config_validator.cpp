#include "config.h"
#include "config_types.h"
#include "config_test_util.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace ccu::fw::config;
using ccutest::TempDir;

// Load from a single root containing common.yaml; cwd is an empty temp dir.
static std::shared_ptr<const ConfigSnapshot>
loadCommon(const std::string& commonYaml, const std::string& svc = "diag") {
    TempDir root;
    root.writeFile("common.yaml", commonYaml);
    return Config::load(svc, LoadOptions{{root.path}, TempDir().path});
}

static bool throwsCode(const std::string& code, const std::string& yaml) {
    try {
        loadCommon(yaml);
    } catch (const ConfigException& e) {
        return e.code == code;
    }
    return false;
}

// ---- CommonLogValidator ----
void test_valid_log_config() {
    auto snap = loadCommon(
        "common:\n  log:\n    schema_version: 1\n    level: INFO\n"
        "    async:\n      enabled: true\n      queue_size: 4096\n"
        "    file:\n      enabled: false\n"
        "    redact:\n      payload_max_bytes: 256\n");
    assert(snap != nullptr);
    std::cout << "  [PASS] test_valid_log_config" << std::endl;
}

void test_invalid_schema_version() {
    assert(throwsCode("CCU-FW-0004",
        "common:\n  log:\n    schema_version: 2\n    level: INFO\n"));
    std::cout << "  [PASS] test_invalid_schema_version" << std::endl;
}

void test_invalid_level() {
    assert(throwsCode("CCU-FW-0004",
        "common:\n  log:\n    schema_version: 1\n    level: VERBOSE\n"));
    std::cout << "  [PASS] test_invalid_level" << std::endl;
}

void test_invalid_queue_size() {
    assert(throwsCode("CCU-FW-0004",
        "common:\n  log:\n    schema_version: 1\n    level: INFO\n"
        "    async:\n      queue_size: 0\n"));
    std::cout << "  [PASS] test_invalid_queue_size" << std::endl;
}

void test_file_enabled_root_missing() {
    assert(throwsCode("CCU-FW-0003",
        "common:\n  log:\n    schema_version: 1\n    level: INFO\n"
        "    file:\n      enabled: true\n"));
    std::cout << "  [PASS] test_file_enabled_root_missing" << std::endl;
}

void test_file_budget_violation() {
    assert(throwsCode("CCU-FW-0004",
        "common:\n  log:\n    schema_version: 1\n    level: INFO\n"
        "    file:\n      enabled: true\n      root: /tmp/ccu\n"
        "      max_file_size_mb: 30\n      max_files: 5\n      total_budget_mb: 100\n"));
    std::cout << "  [PASS] test_file_budget_violation" << std::endl;
}

void test_payload_max_bytes_unlimited() {
    assert(throwsCode("CCU-FW-0004",
        "common:\n  log:\n    schema_version: 1\n    level: INFO\n"
        "    redact:\n      payload_max_bytes: 0\n"));
    std::cout << "  [PASS] test_payload_max_bytes_unlimited" << std::endl;
}

void test_common_log_absent_skipped() {
    auto snap = loadCommon("common:\n  other: value\n");
    assert(snap != nullptr);
    assert(!snap->has("common.log"));
    std::cout << "  [PASS] test_common_log_absent_skipped" << std::endl;
}

// ---- service validator ----
class SecOperationValidator : public Validator {
public:
    void validate(const ConfigSnapshot& snapshot) const override {
        if (!snapshot.has("sec.operation")) {
            throw ConfigException("CCU-FW-0003",
                "sec.operation is required", "", "sec.operation");
        }
    }
};

void test_service_validator_registered() {
    Config::registerValidator("sec", std::make_shared<SecOperationValidator>());
    // missing sec.operation -> 0003
    {
        TempDir root;
        root.writeFile("common.yaml", "common: {}\n");
        bool threw = false;
        try { Config::load("sec", LoadOptions{{root.path}, TempDir().path}); }
        catch (const ConfigException& e) { threw = (e.code == "CCU-FW-0003"); }
        assert(threw);
    }
    // present -> ok
    {
        TempDir root;
        root.writeFile("common.yaml", "common: {}\n");
        root.writeFile("sec.yaml", "sec:\n  operation: read\n");
        auto snap = Config::load("sec", LoadOptions{{root.path}, TempDir().path});
        assert(snap->get<std::string>("sec.operation") == "read");
    }
    std::cout << "  [PASS] test_service_validator_registered" << std::endl;
}

// ---- error code mapping via Config::load ----
void test_required_common_missing_0001() {
    TempDir root; // no common.yaml
    bool threw = false;
    try { Config::load("diag", LoadOptions{{root.path}, TempDir().path}); }
    catch (const ConfigException& e) { threw = (e.code == "CCU-FW-0001"); }
    assert(threw);
    std::cout << "  [PASS] test_required_common_missing_0001" << std::endl;
}

void test_yaml_syntax_error_0002() {
    TempDir root;
    root.writeFile("common.yaml", "common: [unclosed\n");
    bool threw = false;
    try { Config::load("diag", LoadOptions{{root.path}, TempDir().path}); }
    catch (const ConfigException& e) { threw = (e.code == "CCU-FW-0002"); }
    assert(threw);
    std::cout << "  [PASS] test_yaml_syntax_error_0002" << std::endl;
}

void test_invalid_service_name_0004() {
    bool threw = false;
    try { Config::load("../bad", LoadOptions{}); }
    catch (const ConfigException& e) { threw = (e.code == "CCU-FW-0004"); }
    assert(threw);
    std::cout << "  [PASS] test_invalid_service_name_0004" << std::endl;
}

// ---- ConfigSnapshot template dispatch (via load) ----
void test_snapshot_template_getters() {
    TempDir root;
    root.writeFile("common.yaml",
        "common:\n  log:\n    schema_version: 1\n    level: WARN\n"
        "    async:\n      queue_size: 8192\n      enabled: true\n"
        "diag:\n  ecu: BMS\n  modules:\n    router: WARN\n    tester: INFO\n");
    auto snap = Config::load("diag", LoadOptions{{root.path}, TempDir().path});

    assert(snap->get<std::string>("common.log.level") == "WARN");
    assert(snap->get<int64_t>("common.log.async.queue_size") == 8192);
    assert(snap->get<bool>("common.log.async.enabled") == true);
    assert(snap->get<std::string>("diag.ecu") == "BMS");
    assert(snap->get<std::string>("diag.modules.router") == "WARN");

    // getOr: missing -> default
    assert(snap->getOr<std::string>("common.log.missing", "fallback") == "fallback");
    assert(snap->getOr("common.log.missing", "fallback2") == "fallback2"); // const char* overload
    assert(snap->getOr<int64_t>("common.log.missing", 99) == 99);

    // getOr: present but type mismatch -> throws (not default)
    bool threw = false;
    try { snap->getOr<int64_t>("common.log.level", 7); }
    catch (const ConfigException& e) { threw = (e.code == "CCU-FW-0004"); }
    assert(threw);

    // has / keys
    assert(snap->has("common.log.level"));
    assert(!snap->has("common.log.nope"));
    auto mods = snap->keys("diag.modules");
    assert(mods.size() == 2);

    std::cout << "  [PASS] test_snapshot_template_getters" << std::endl;
}

int main() {
    std::cout << "Running Config validator/load tests..." << std::endl;
    test_valid_log_config();
    test_invalid_schema_version();
    test_invalid_level();
    test_invalid_queue_size();
    test_file_enabled_root_missing();
    test_file_budget_violation();
    test_payload_max_bytes_unlimited();
    test_common_log_absent_skipped();
    test_service_validator_registered();
    test_required_common_missing_0001();
    test_yaml_syntax_error_0002();
    test_invalid_service_name_0004();
    test_snapshot_template_getters();
    std::cout << "All Config validator/load tests passed!" << std::endl;
    return 0;
}
