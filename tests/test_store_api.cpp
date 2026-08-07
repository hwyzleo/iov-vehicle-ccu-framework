#include "store.h"
#include "store_types.h"
#include "store_test_util.h"

#include <cassert>
#include <fstream>
#include <iostream>

using namespace cgw::fw::store;
using cgwtest::TempDir;
namespace fs = std::filesystem;

static Store openStore(const fs::path& root, FlushMode mode = FlushMode::Synchronous) {
    StoreOptions opt;
    opt.root = root;
    opt.flushMode = mode;
    return Store::open("diag", opt);
}

void test_save_load_roundtrip() {
    TempDir tmp;
    Store s = openStore(tmp.path);
    s.save<std::string>("name", "cgw-diag");
    s.save<int64_t>("counter", 42);
    s.save<double>("ratio", 1.25);
    s.save<bool>("flag", true);
    s.save<std::vector<std::string>>("tags", {"a", "b"});
    s.save<std::vector<int64_t>>("ids", {1, 2, 3});

    assert(s.load<std::string>("name") == "cgw-diag");
    assert(s.load<int64_t>("counter") == 42);
    assert(s.load<double>("ratio") == 1.25);
    assert(s.load<bool>("flag") == true);
    assert((s.load<std::vector<std::string>>("tags") == std::vector<std::string>{"a", "b"}));
    assert((s.load<std::vector<int64_t>>("ids") == std::vector<int64_t>{1, 2, 3}));
    std::cout << "  [PASS] test_save_load_roundtrip" << std::endl;
}

void test_load_missing_throws_0105() {
    TempDir tmp;
    Store s = openStore(tmp.path);
    bool threw = false;
    try {
        s.load<int64_t>("nope");
    } catch (const StoreException& e) {
        threw = true;
        assert(e.code == "CGW-FW-0105");
    }
    assert(threw);
    std::cout << "  [PASS] test_load_missing_throws_0105" << std::endl;
}

void test_loadOr_missing_returns_default() {
    TempDir tmp;
    Store s = openStore(tmp.path);
    assert(s.loadOr<int64_t>("nope", 99) == 99);
    assert(s.loadOr<std::string>("nope", "fallback") == "fallback");
    assert(s.loadOr("nope", "lit") == "lit");
    s.save<int64_t>("k", 7);
    assert(s.loadOr<int64_t>("k", 99) == 7);
    std::cout << "  [PASS] test_loadOr_missing_returns_default" << std::endl;
}

void test_loadOr_does_not_swallow_0104() {
    TempDir tmp;
    Store s = openStore(tmp.path);
    // Write a corrupt final file directly.
    fs::path finalP = tmp.path / "diag" / "k.dat";
    fs::create_directories(finalP.parent_path());
    { std::ofstream f(finalP, std::ios::binary); f << "garbage"; }
    bool threw = false;
    try {
        s.loadOr<int64_t>("k", 99);
    } catch (const StoreException& e) {
        threw = true;
        assert(e.code == "CGW-FW-0104");
    }
    assert(threw);
    std::cout << "  [PASS] test_loadOr_does_not_swallow_0104" << std::endl;
}

void test_has() {
    TempDir tmp;
    Store s = openStore(tmp.path);
    assert(!s.has("k"));
    s.save<int64_t>("k", 1);
    assert(s.has("k"));
    std::cout << "  [PASS] test_has" << std::endl;
}

void test_remove_idempotent() {
    TempDir tmp;
    Store s = openStore(tmp.path);
    assert(!s.remove("k")); // missing -> false (idempotent)
    s.save<int64_t>("k", 1);
    assert(s.has("k"));
    assert(s.remove("k")); // removed
    assert(!s.has("k"));
    assert(!s.remove("k")); // idempotent again
    std::cout << "  [PASS] test_remove_idempotent" << std::endl;
}

void test_sync_is_dirty_false() {
    TempDir tmp;
    Store s = openStore(tmp.path);
    s.save<int64_t>("k", 1);
    assert(!s.isDirty("k"));
    s.flush(); // no-op, must not throw
    std::cout << "  [PASS] test_sync_is_dirty_false" << std::endl;
}

void test_invalid_key_throws_0101() {
    TempDir tmp;
    Store s = openStore(tmp.path);
    for (std::string bad : {"", ".", "..", "a/b", "a\\b", "a..b", ".a", "a."}) {
        bool threw = false;
        try { s.save<int64_t>(bad, 1); } catch (const StoreException& e) {
            threw = true; assert(e.code == "CGW-FW-0101");
        }
        assert(threw);
    }
    std::cout << "  [PASS] test_invalid_key_throws_0101" << std::endl;
}

void test_file_permissions_0600() {
    TempDir tmp;
    Store s = openStore(tmp.path);
    s.save<int64_t>("k", 1);
    fs::path finalP = tmp.path / "diag" / "k.dat";
    using fs::perms;
    auto perm = fs::status(finalP).permissions();
    assert((perm & perms::group_all) == perms::none);
    assert((perm & perms::others_all) == perms::none);
    std::cout << "  [PASS] test_file_permissions_0600" << std::endl;
}

void test_reopen_reads_persisted() {
    TempDir tmp;
    {
        Store s = openStore(tmp.path);
        s.save<int64_t>("counter", 123);
        s.save<std::string>("name", "diag");
    }
    // Reopen (simulates restart) and read.
    Store s2 = openStore(tmp.path);
    assert(s2.load<int64_t>("counter") == 123);
    assert(s2.load<std::string>("name") == "diag");
    std::cout << "  [PASS] test_reopen_reads_persisted" << std::endl;
}

void test_overwrite_then_load() {
    TempDir tmp;
    Store s = openStore(tmp.path);
    s.save<int64_t>("k", 1);
    s.save<int64_t>("k", 2);
    s.save<int64_t>("k", 3);
    assert(s.load<int64_t>("k") == 3);
    std::cout << "  [PASS] test_overwrite_then_load" << std::endl;
}

void test_loadOr_string_literal_default() {
    TempDir tmp;
    Store s = openStore(tmp.path);
    assert(s.loadOr("missing", "default-val") == "default-val");
    s.save<std::string>("missing", "x");
    assert(s.loadOr("missing", "default-val") == "x");
    std::cout << "  [PASS] test_loadOr_string_literal_default" << std::endl;
}

int main() {
    test_save_load_roundtrip();
    test_load_missing_throws_0105();
    test_loadOr_missing_returns_default();
    test_loadOr_does_not_swallow_0104();
    test_has();
    test_remove_idempotent();
    test_sync_is_dirty_false();
    test_invalid_key_throws_0101();
    test_file_permissions_0600();
    test_reopen_reads_persisted();
    test_overwrite_then_load();
    test_loadOr_string_literal_default();
    std::cout << "All store_api tests passed." << std::endl;
    return 0;
}
