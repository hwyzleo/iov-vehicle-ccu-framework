#include "store/store_atomic_writer.h"
#include "store_types.h"
#include "store_test_util.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <unistd.h>

using namespace ccu::fw::store;
using ccutest::TempDir;
namespace fs = std::filesystem;

static bool isRoot() { return ::geteuid() == 0; }

static std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
    return s;
}

void test_commit_success() {
    TempDir tmp;
    fs::path dir = tmp.mkdir("svc");
    fs::path finalP = dir / "k.dat";
    std::vector<uint8_t> bytes{'h', 'e', 'l', 'l', 'o'};
    AtomicWriter::commit(finalP, dir, "svc", "k", bytes, 1024);
    assert(fs::exists(finalP));
    assert(readFile(finalP) == "hello");
    // temp files cleaned up
    int temps = 0;
    for (auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() == ".tmp") ++temps;
    }
    assert(temps == 0);
    std::cout << "  [PASS] test_commit_success" << std::endl;
}

void test_commit_overwrite() {
    TempDir tmp;
    fs::path dir = tmp.mkdir("svc");
    fs::path finalP = dir / "k.dat";
    AtomicWriter::commit(finalP, dir, "svc", "k",
                         std::vector<uint8_t>{'o', 'l', 'd'}, 1024);
    assert(readFile(finalP) == "old");
    AtomicWriter::commit(finalP, dir, "svc", "k",
                         std::vector<uint8_t>{'n', 'e', 'w'}, 1024);
    assert(readFile(finalP) == "new");
    std::cout << "  [PASS] test_commit_overwrite" << std::endl;
}

void test_commit_file_permissions_0600() {
    TempDir tmp;
    fs::path dir = tmp.mkdir("svc");
    fs::path finalP = dir / "k.dat";
    AtomicWriter::commit(finalP, dir, "svc", "k",
                         std::vector<uint8_t>{'x'}, 1024);
    using fs::perms;
    auto perm = fs::status(finalP).permissions();
    assert((perm & perms::group_all) == perms::none);
    assert((perm & perms::others_all) == perms::none);
    std::cout << "  [PASS] test_commit_file_permissions_0600" << std::endl;
}

void test_commit_size_limit() {
    TempDir tmp;
    fs::path dir = tmp.mkdir("svc");
    fs::path finalP = dir / "k.dat";
    std::vector<uint8_t> big(100, 'z');
    bool threw = false;
    try {
        AtomicWriter::commit(finalP, dir, "svc", "k", big, 10);
    } catch (const StoreException& e) {
        threw = true;
        assert(e.code == "CCU-FW-0104");
    }
    assert(threw);
    assert(!fs::exists(finalP));
    std::cout << "  [PASS] test_commit_size_limit" << std::endl;
}

void test_commit_readonly_dir_preserves_old() {
    if (isRoot()) {
        std::cout << "  [SKIP] test_commit_readonly_dir_preserves_old (root)" << std::endl;
        return;
    }
    TempDir tmp;
    fs::path dir = tmp.mkdir("svc");
    fs::path finalP = dir / "k.dat";
    AtomicWriter::commit(finalP, dir, "svc", "k",
                         std::vector<uint8_t>{'o', 'l', 'd'}, 1024);
    // Make dir read-only (no write).
    fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace);
    bool threw = false;
    try {
        AtomicWriter::commit(finalP, dir, "svc", "k",
                             std::vector<uint8_t>{'n', 'e', 'w'}, 1024);
    } catch (const StoreException& e) {
        threw = true;
        assert(e.code == "CCU-FW-0102");
        assert(e.stage == "temp-create");
    }
    assert(threw);
    assert(readFile(finalP) == "old"); // old preserved
    fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace);
    std::cout << "  [PASS] test_commit_readonly_dir_preserves_old" << std::endl;
}

void test_commit_temp_uniqueness() {
    TempDir tmp;
    fs::path dir = tmp.mkdir("svc");
    // Rapidly commit many keys; all should succeed without temp collision.
    for (int i = 0; i < 50; ++i) {
        fs::path finalP = dir / (std::string("k") + std::to_string(i) + ".dat");
        std::vector<uint8_t> b(1, static_cast<uint8_t>(i));
        AtomicWriter::commit(finalP, dir, "svc", std::to_string(i), b, 1024);
    }
    int count = 0;
    for (auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() == ".dat") ++count;
    }
    assert(count == 50);
    std::cout << "  [PASS] test_commit_temp_uniqueness" << std::endl;
}

void test_sync_dir() {
    TempDir tmp;
    fs::path dir = tmp.mkdir("svc");
    AtomicWriter::syncDir(dir, "svc", "k", "test");
    // Should not throw; if it did, we wouldn't reach here.
    std::cout << "  [PASS] test_sync_dir" << std::endl;
}

int main() {
    test_commit_success();
    test_commit_overwrite();
    test_commit_file_permissions_0600();
    test_commit_size_limit();
    test_commit_readonly_dir_preserves_old();
    test_commit_temp_uniqueness();
    test_sync_dir();
    std::cout << "All store_atomic_writer tests passed." << std::endl;
    return 0;
}
