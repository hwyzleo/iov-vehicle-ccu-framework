#include "store.h"
#include "store_types.h"
#include "store_test_util.h"

#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

using namespace cgw::fw::store;
using cgwtest::TempDir;
namespace fs = std::filesystem;

static Store openStore(const fs::path& root) {
    StoreOptions opt;
    opt.root = root;
    opt.lockTimeout = std::chrono::seconds(5);
    return Store::open("diag", opt);
}

void test_multithread_same_key() {
    TempDir tmp;
    Store s = openStore(tmp.path);
    const int N = 8;
    const int iters = 200;
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < N; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < iters; ++i) {
                try {
                    s.save<int64_t>("counter", static_cast<int64_t>(t) * iters + i);
                } catch (const StoreException&) {
                    ++errors;
                }
            }
        });
    }
    for (auto& th : threads) th.join();
    assert(errors.load() == 0);
    // Final value must be a valid int64 (no corruption / decode failure).
    int64_t v = s.load<int64_t>("counter");
    (void)v;
    // No leftover temp files.
    int temps = 0;
    for (auto& e : fs::directory_iterator(tmp.path / "diag")) {
        if (e.path().extension() == ".tmp") ++temps;
    }
    assert(temps == 0);
    std::cout << "  [PASS] test_multithread_same_key" << std::endl;
}

void test_multithread_different_keys() {
    TempDir tmp;
    Store s = openStore(tmp.path);
    const int N = 8;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};
    for (int t = 0; t < N; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < 100; ++i) {
                try {
                    std::string key = "k" + std::to_string(t);
                    s.save<int64_t>(key, i);
                    assert(s.load<int64_t>(key) == i);
                } catch (const StoreException&) {
                    ++errors;
                }
            }
        });
    }
    for (auto& th : threads) th.join();
    assert(errors.load() == 0);
    for (int t = 0; t < N; ++t) {
        assert(s.load<int64_t>("k" + std::to_string(t)) == 99);
    }
    std::cout << "  [PASS] test_multithread_different_keys" << std::endl;
}

void test_multithread_mixed_ops() {
    TempDir tmp;
    Store s = openStore(tmp.path);
    s.save<int64_t>("shared", 0);
    std::atomic<bool> stop{false};
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    // Writer
    threads.emplace_back([&]() {
        for (int i = 0; i < 500; ++i) {
            try { s.save<int64_t>("shared", i); }
            catch (const StoreException&) { ++errors; }
        }
        stop = true;
    });
    // Reader
    threads.emplace_back([&]() {
        while (!stop) {
            try { (void)s.load<int64_t>("shared"); }
            catch (const StoreException& e) {
                // 0105 acceptable transient between save/remove
                if (e.code != "CGW-FW-0105") ++errors;
            }
        }
    });
    // Remover + re-saver
    threads.emplace_back([&]() {
        for (int i = 0; i < 50; ++i) {
            try { s.remove("shared"); s.save<int64_t>("shared", i); }
            catch (const StoreException&) { ++errors; }
        }
    });
    for (auto& th : threads) th.join();
    assert(errors.load() == 0);
    std::cout << "  [PASS] test_multithread_mixed_ops" << std::endl;
}

void test_multi_instance_same_key() {
    TempDir tmp;
    Store s1 = openStore(tmp.path);
    Store s2 = openStore(tmp.path); // same service+root
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    threads.emplace_back([&]() {
        for (int i = 0; i < 300; ++i) {
            try { s1.save<int64_t>("k", i); }
            catch (const StoreException&) { ++errors; }
        }
    });
    threads.emplace_back([&]() {
        for (int i = 0; i < 300; ++i) {
            try { s2.save<int64_t>("k", i + 1000); }
            catch (const StoreException&) { ++errors; }
        }
    });
    for (auto& th : threads) th.join();
    assert(errors.load() == 0);
    // No corruption: load decodes cleanly.
    (void)s1.load<int64_t>("k");
    std::cout << "  [PASS] test_multi_instance_same_key" << std::endl;
}

int main() {
    test_multithread_same_key();
    test_multithread_different_keys();
    test_multithread_mixed_ops();
    test_multi_instance_same_key();
    std::cout << "All store_concurrency tests passed." << std::endl;
    return 0;
}
