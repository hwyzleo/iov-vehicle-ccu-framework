#include "config/config_path_resolver.h"
#include "config_types.h"
#include "config_test_util.h"
#include <cassert>
#include <iostream>
#include <algorithm>

using namespace cgw::fw::config;
using cgwtest::TempDir;

static bool hasSource(const std::vector<ConfigEntry>& entries, const std::string& source) {
    return std::any_of(entries.begin(), entries.end(),
                       [&](const ConfigEntry& e) { return e.source == source; });
}

static const ConfigEntry* findSource(const std::vector<ConfigEntry>& entries, const std::string& source) {
    for (const auto& e : entries) if (e.source == source) return &e;
    return nullptr;
}

// --- service name validation ---
void test_service_name_valid() {
    assert(PathResolver::isValidServiceName("diag"));
    assert(PathResolver::isValidServiceName("fota"));
    assert(PathResolver::isValidServiceName("sec"));
    std::cout << "  [PASS] test_service_name_valid" << std::endl;
}

void test_service_name_invalid() {
    assert(!PathResolver::isValidServiceName(""));
    assert(!PathResolver::isValidServiceName("."));
    assert(!PathResolver::isValidServiceName(".."));
    assert(!PathResolver::isValidServiceName("a/b"));
    assert(!PathResolver::isValidServiceName("/abs"));
    assert(!PathResolver::isValidServiceName("a\\b"));
    assert(!PathResolver::isValidServiceName("diag..x"));
    std::cout << "  [PASS] test_service_name_invalid" << std::endl;
}

// --- root normalization ---
void test_normalize_roots_dedupe_and_trailing_slash() {
    std::vector<std::filesystem::path> roots = {"/etc/cgw/", "/etc/cgw", "/tmp/x"};
    auto out = PathResolver::normalizeRoots(roots);
    assert(out.size() == 2);
    assert(out[0] == "/etc/cgw");
    assert(out[1] == "/tmp/x");
    std::cout << "  [PASS] test_normalize_roots_dedupe_and_trailing_slash" << std::endl;
}

void test_normalize_roots_reject_empty() {
    bool threw = false;
    try {
        PathResolver::normalizeRoots({""});
    } catch (const ConfigException& e) {
        threw = true;
        assert(e.code == "CGW-FW-0004");
    }
    assert(threw);
    std::cout << "  [PASS] test_normalize_roots_reject_empty" << std::endl;
}

// --- six-stage full ---
void test_six_stage_all_layers() {
    TempDir roots;
    TempDir proj;
    roots.writeFile("common.yaml", "common: {log: {level: INFO}}");
    roots.writeFile("conf.d/diag.yaml", "diag: {a: 1}");
    roots.writeFile("diag.yaml", "diag: {root0: true}");
    proj.writeFile("config/common.yaml", "common: {log: {level: DEBUG}}");
    proj.writeFile("config/diag.yaml", "diag: {proj: true}");
    proj.writeFile("diag.yaml", "diag: {local: true}");

    auto entries = PathResolver::resolveAll("diag", {roots.path}, proj.path);
    assert(entries.size() == 6);
    assert(entries[0].source == "common");
    assert(entries[1].source == "project/common");
    assert(entries[2].source == "conf.d/diag");
    assert(entries[3].source == "project/diag");
    assert(entries[4].source == "root[0]/diag");
    assert(entries[5].source == "local/diag");
    std::cout << "  [PASS] test_six_stage_all_layers" << std::endl;
}

// --- compatibility: new layers absent == original 3-layer ---
void test_compat_three_layer() {
    TempDir roots;
    TempDir proj; // empty cwd
    roots.writeFile("common.yaml", "common: {log: {level: INFO}}");
    roots.writeFile("conf.d/diag.yaml", "diag: {a: 1}");
    proj.writeFile("diag.yaml", "diag: {local: true}");

    auto entries = PathResolver::resolveAll("diag", {roots.path}, proj.path);
    assert(entries.size() == 3);
    assert(entries[0].source == "common");
    assert(entries[1].source == "conf.d/diag");
    assert(entries[2].source == "local/diag");
    std::cout << "  [PASS] test_compat_three_layer" << std::endl;
}

// --- first-match common.yaml ---
void test_first_match_common() {
    TempDir rootsA;
    TempDir rootsB;
    rootsA.writeFile("common.yaml", "common: {a: 1}");
    rootsB.writeFile("common.yaml", "common: {b: 2}");
    auto entries = PathResolver::resolveAll("diag", {rootsA.path, rootsB.path}, TempDir().path);
    assert(entries.size() == 1);
    assert(entries[0].source == "common");
    // must be rootsA's file (index 0)
    assert(entries[0].path == rootsA.path / "common.yaml");
    std::cout << "  [PASS] test_first_match_common" << std::endl;
}

// --- first-match conf.d ---
void test_first_match_conf_d() {
    TempDir rootsA;
    TempDir rootsB;
    TempDir proj;
    rootsA.writeFile("common.yaml", "common: {}");
    rootsA.writeFile("conf.d/diag.yaml", "diag: {from: A}");
    rootsB.writeFile("conf.d/diag.yaml", "diag: {from: B}");
    auto entries = PathResolver::resolveAll("diag", {rootsA.path, rootsB.path}, proj.path);
    assert(hasSource(entries, "common"));
    assert(hasSource(entries, "conf.d/diag"));
    const auto* e = findSource(entries, "conf.d/diag");
    assert(e && e->path == rootsA.path / "conf.d" / "diag.yaml");
    std::cout << "  [PASS] test_first_match_conf_d" << std::endl;
}

// --- ordered-overlay root direct svc ---
void test_ordered_overlay_root_svc() {
    TempDir rootsA;
    TempDir rootsB;
    TempDir proj;
    rootsA.writeFile("common.yaml", "common: {}");
    rootsA.writeFile("diag.yaml", "diag: {root0: true}");
    rootsB.writeFile("diag.yaml", "diag: {root1: true}");
    auto entries = PathResolver::resolveAll("diag", {rootsA.path, rootsB.path}, proj.path);
    assert(hasSource(entries, "root[0]/diag"));
    assert(hasSource(entries, "root[1]/diag"));
    std::cout << "  [PASS] test_ordered_overlay_root_svc" << std::endl;
}

// --- required common missing ---
void test_required_common_missing() {
    TempDir roots; // no common.yaml
    TempDir proj;
    bool threw = false;
    try {
        PathResolver::resolveAll("diag", {roots.path}, proj.path);
    } catch (const ConfigException& e) {
        threw = true;
        assert(e.code == "CGW-FW-0001");
    }
    assert(threw);
    std::cout << "  [PASS] test_required_common_missing" << std::endl;
}

// --- found-but-not-regular (common.yaml is a directory) -> 0001 no fallback ---
void test_found_not_regular_no_fallback() {
    TempDir rootsA;
    TempDir rootsB;
    rootsA.makeDir("common.yaml"); // a directory, not a regular file
    rootsB.writeFile("common.yaml", "common: {}"); // a valid fallback candidate
    bool threw = false;
    try {
        PathResolver::resolveAll("diag", {rootsA.path, rootsB.path}, TempDir().path);
    } catch (const ConfigException& e) {
        threw = true;
        assert(e.code == "CGW-FW-0001");
    }
    assert(threw); // must NOT fall back to rootsB
    std::cout << "  [PASS] test_found_not_regular_no_fallback" << std::endl;
}

int main() {
    std::srand(12345);
    std::cout << "Running PathResolver tests..." << std::endl;
    test_service_name_valid();
    test_service_name_invalid();
    test_normalize_roots_dedupe_and_trailing_slash();
    test_normalize_roots_reject_empty();
    test_six_stage_all_layers();
    test_compat_three_layer();
    test_first_match_common();
    test_first_match_conf_d();
    test_ordered_overlay_root_svc();
    test_required_common_missing();
    test_found_not_regular_no_fallback();
    std::cout << "All PathResolver tests passed!" << std::endl;
    return 0;
}
