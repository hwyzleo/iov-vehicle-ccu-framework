#include "store/store_path_resolver.h"
#include "store_types.h"
#include "store_test_util.h"

#include <cassert>
#include <iostream>
#include <unistd.h>

using namespace cgw::fw::store;
using cgwtest::TempDir;
namespace fs = std::filesystem;

static bool isRoot() { return ::geteuid() == 0; }

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

void test_key_valid() {
    assert(PathResolver::isValidKey("counter"));
    assert(PathResolver::isValidKey("diag.counter"));
    assert(PathResolver::isValidKey("fota.snapshot.seq"));
    assert(PathResolver::isValidKey("a.b.c.d"));
    std::cout << "  [PASS] test_key_valid" << std::endl;
}

void test_key_invalid() {
    assert(!PathResolver::isValidKey(""));
    assert(!PathResolver::isValidKey("."));
    assert(!PathResolver::isValidKey(".."));
    assert(!PathResolver::isValidKey("a/b"));
    assert(!PathResolver::isValidKey("a\\b"));
    assert(!PathResolver::isValidKey("a..b"));   // empty segment
    assert(!PathResolver::isValidKey(".a"));     // leading dot
    assert(!PathResolver::isValidKey("a."));     // trailing dot
    assert(!PathResolver::isValidKey("a...b"));  // empty segment
    assert(!PathResolver::isValidKey("..x"));
    assert(!PathResolver::isValidKey("x.."));
    std::cout << "  [PASS] test_key_invalid" << std::endl;
}

void test_normalize_root() {
    assert(PathResolver::normalizeRoot("/var/lib/cgw") == "/var/lib/cgw");
    assert(PathResolver::normalizeRoot("/var/lib/cgw/") == "/var/lib/cgw");
    assert(PathResolver::normalizeRoot("/var/lib/cgw/../cgw") == "/var/lib/cgw");
    bool threw = false;
    try { PathResolver::normalizeRoot(""); } catch (const StoreException& e) {
        threw = true; assert(e.code == "CGW-FW-0101");
    }
    assert(threw);
    std::cout << "  [PASS] test_normalize_root" << std::endl;
}

void test_path_resolution() {
    fs::path root = "/var/lib/cgw";
    assert(PathResolver::serviceDir(root, "diag") == "/var/lib/cgw/diag");
    assert(PathResolver::finalFile("/var/lib/cgw/diag", "counter") ==
           "/var/lib/cgw/diag/counter.dat");
    assert(PathResolver::finalFile("/var/lib/cgw/diag", "snap.seq") ==
           "/var/lib/cgw/diag/snap.seq.dat");
    assert(PathResolver::lockFile("/var/lib/cgw/diag", "counter") ==
           "/var/lib/cgw/diag/.counter.lock");
    std::cout << "  [PASS] test_path_resolution" << std::endl;
}

void test_ensure_service_dir_creates_0700() {
    TempDir tmp;
    fs::path root = tmp.path / "root";
    PathResolver::ensureServiceDir(root, "diag", "diag");
    fs::path dir = root / "diag";
    assert(fs::is_directory(dir));
    auto perm = fs::status(dir).permissions();
    using fs::perms;
    assert((perm & perms::group_all) == perms::none);
    assert((perm & perms::others_all) == perms::none);
    assert((perm & perms::owner_all) == perms::owner_all);
    std::cout << "  [PASS] test_ensure_service_dir_creates_0700" << std::endl;
}

void test_ensure_service_dir_rejects_symlink() {
    TempDir tmp;
    fs::path root = tmp.path / "root";
    fs::create_directories(root);
    // Create a symlink <root>/diag -> /tmp (escape attempt).
    fs::create_directory_symlink(fs::temp_directory_path(), root / "diag");
    bool threw = false;
    try {
        PathResolver::ensureServiceDir(root, "diag", "diag");
    } catch (const StoreException& e) {
        threw = true;
        assert(e.code == "CGW-FW-0101");
    }
    assert(threw);
    std::cout << "  [PASS] test_ensure_service_dir_rejects_symlink" << std::endl;
}

void test_ensure_service_dir_readonly_root_fails() {
    if (isRoot()) {
        std::cout << "  [SKIP] test_ensure_service_dir_readonly_root_fails (running as root)" << std::endl;
        return;
    }
    TempDir tmp;
    fs::path root = tmp.path / "root";
    fs::create_directories(root);
    tmp.makeReadOnly("root"); // root read-only, no write/exec for owner? we set read+exec
    // Actually make dir not traversable: remove exec too.
    fs::permissions(root, fs::perms::owner_read, fs::perm_options::replace);
    bool threw = false;
    try {
        PathResolver::ensureServiceDir(root, "diag", "diag");
    } catch (const StoreException& e) {
        threw = true;
        assert(e.code == "CGW-FW-0101");
    }
    assert(threw);
    // restore for cleanup
    fs::permissions(root, fs::perms::owner_all, fs::perm_options::replace);
    std::cout << "  [PASS] test_ensure_service_dir_readonly_root_fails" << std::endl;
}

int main() {
    std::srand(1234);
    test_service_name_valid();
    test_service_name_invalid();
    test_key_valid();
    test_key_invalid();
    test_normalize_root();
    test_path_resolution();
    test_ensure_service_dir_creates_0700();
    test_ensure_service_dir_rejects_symlink();
    test_ensure_service_dir_readonly_root_fails();
    std::cout << "All store_path_resolver tests passed." << std::endl;
    return 0;
}
