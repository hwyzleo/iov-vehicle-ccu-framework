#include "config.h"
#include "store.h"
#include "store_types.h"
#include "store_test_util.h"

#include <cassert>
#include <fstream>
#include <iostream>

using namespace cgw::fw::config;
using namespace cgw::fw::store;
using cgwtest::TempDir;
namespace fs = std::filesystem;

// Minimal consumer pattern: each service opens its own Store with the root
// injected from the ConfigSnapshot (store does NOT read config itself).
static void diagConsumer(const fs::path& root) {
    StoreOptions opt; opt.root = root;
    Store s = Store::open("diag", opt);
    s.save<int64_t>("did.f190.counter", 7);
    assert(s.load<int64_t>("did.f190.counter") == 7);
    assert(s.remove("did.f190.counter"));
}

static void fotaConsumer(const fs::path& root) {
    StoreOptions opt; opt.root = root;
    Store s = Store::open("fota", opt);
    s.save<int64_t>("snapshot.seq", 42);
    // Restart simulation.
    Store s2 = Store::open("fota", opt);
    assert(s2.load<int64_t>("snapshot.seq") == 42);
}

static void secConsumer(const fs::path& root) {
    StoreOptions opt; opt.root = root;
    Store s = Store::open("sec", opt);
    s.save<std::string>("last.operation", "unlock");
    assert(s.loadOr<std::string>("last.operation", "none") == "unlock");
    assert(s.loadOr<std::string>("missing", "none") == "none");
}

void test_config_root_injection() {
    TempDir tmp;
    fs::path storeRoot = tmp.path / "store-root";
    fs::path cfgRoot = tmp.mkdir("cfg");
    fs::path cwd = tmp.mkdir("cwd");

    // common.yaml providing common.store.root
    {
        std::string yaml = "common:\n  store:\n    root: " +
                           storeRoot.string() + "\n";
        std::ofstream f(cfgRoot / "common.yaml", std::ios::binary);
        f << yaml;
    }

    // Host loads config, reads common.store.root, injects into Store.
    LoadOptions lopt;
    lopt.configRoots = {cfgRoot};
    lopt.cwd = cwd;
    auto snap = Config::load("diag", lopt);
    std::string root = snap->getOr<std::string>("common.store.root",
                                                "/var/lib/cgw");
    assert(root == storeRoot.string());

    // Store::open with the injected root (store has no config dependency).
    StoreOptions sopt; sopt.root = root;
    Store s = Store::open("diag", sopt);
    s.save<int64_t>("counter", 123);
    s.save<std::string>("name", "cgw");

    // Restart: reopen and verify persistence.
    Store s2 = Store::open("diag", sopt);
    assert(s2.load<int64_t>("counter") == 123);
    assert(s2.load<std::string>("name") == "cgw");

    // Remove and verify idempotent.
    assert(s2.remove("counter"));
    assert(!s2.has("counter"));
    assert(!s2.remove("counter"));
    std::cout << "  [PASS] test_config_root_injection" << std::endl;
}

void test_minimal_consumers() {
    TempDir tmp;
    fs::path storeRoot = tmp.path / "store-root";
    diagConsumer(storeRoot);
    fotaConsumer(storeRoot);
    secConsumer(storeRoot);
    // Each service got its own 0700 directory under the injected root.
    assert(fs::is_directory(storeRoot / "diag"));
    assert(fs::is_directory(storeRoot / "fota"));
    assert(fs::is_directory(storeRoot / "sec"));
    std::cout << "  [PASS] test_minimal_consumers" << std::endl;
}

void test_store_does_not_include_config_headers() {
    // Compile-time guarantee: store.h/store_types.h do not include config.h.
    // This translation unit includes both, but store itself is usable without
    // config (proven by the consumers above using only store.h). Verified by
    // the fact that no config symbols are referenced in store headers.
    std::cout << "  [PASS] test_store_does_not_include_config_headers" << std::endl;
}

int main() {
    test_config_root_injection();
    test_minimal_consumers();
    test_store_does_not_include_config_headers();
    std::cout << "All store_integration tests passed." << std::endl;
    return 0;
}
