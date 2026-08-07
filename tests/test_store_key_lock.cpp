#include "store/store_key_lock_manager.h"
#include "store/store_path_resolver.h"
#include "store_types.h"
#include "store_test_util.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <fcntl.h>

using namespace cgw::fw::store;
using cgwtest::TempDir;
namespace fs = std::filesystem;

void test_acquire_release() {
    TempDir tmp;
    fs::path dir = tmp.mkdir("svc");
    KeyLockManager mgr(dir, std::chrono::milliseconds(500));
    auto h = mgr.acquire("k", "svc");
    assert(h != nullptr);
    h.reset(); // releases
    // Re-acquire after release.
    auto h2 = mgr.acquire("k", "svc");
    assert(h2 != nullptr);
    std::cout << "  [PASS] test_acquire_release" << std::endl;
}

void test_same_key_serialized() {
    TempDir tmp;
    fs::path dir = tmp.mkdir("svc");
    KeyLockManager mgr(dir, std::chrono::milliseconds(100));
    auto h = mgr.acquire("k", "svc");
    assert(h != nullptr);
    // Another thread tries the same key with a short timeout -> 0103.
    bool threw = false;
    std::thread t([&]() {
        try {
            auto h2 = mgr.acquire("k", "svc");
            // If acquired (shouldn't within timeout), release.
            h2.reset();
        } catch (const StoreException& e) {
            threw = (e.code == "CGW-FW-0103");
        }
    });
    t.join();
    assert(threw);
    h.reset();
    // Now acquirable.
    auto h3 = mgr.acquire("k", "svc");
    assert(h3 != nullptr);
    std::cout << "  [PASS] test_same_key_serialized" << std::endl;
}

void test_different_key_parallel() {
    TempDir tmp;
    fs::path dir = tmp.mkdir("svc");
    KeyLockManager mgr(dir, std::chrono::milliseconds(500));
    auto h1 = mgr.acquire("k1", "svc");
    assert(h1 != nullptr);
    // Different key should acquire immediately while k1 held.
    auto h2 = mgr.acquire("k2", "svc");
    assert(h2 != nullptr);
    std::cout << "  [PASS] test_different_key_parallel" << std::endl;
}

void test_cross_process_flock() {
    TempDir tmp;
    fs::path dir = tmp.mkdir("svc");
    fs::path lockP = PathResolver::lockFile(dir, "k");

    pid_t pid = ::fork();
    if (pid == 0) {
        // Child: hold the cross-process flock, sleep, exit (releases flock).
        int fd = ::open(lockP.c_str(), O_CREAT | O_RDWR, 0600);
        if (fd < 0) _exit(1);
        if (::flock(fd, LOCK_EX) != 0) _exit(2);
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        ::close(fd);
        _exit(0);
    }
    // Parent: wait briefly for child to acquire, then attempt store lock with
    // a short timeout -> expect 0103.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    KeyLockManager mgr(dir, std::chrono::milliseconds(100));
    bool threw = false;
    try {
        auto h = mgr.acquire("k", "svc");
        h.reset();
    } catch (const StoreException& e) {
        threw = (e.code == "CGW-FW-0103");
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    assert(threw);

    // After child exits (flock released), acquire should succeed.
    KeyLockManager mgr2(dir, std::chrono::milliseconds(500));
    auto h = mgr2.acquire("k", "svc");
    assert(h != nullptr);
    std::cout << "  [PASS] test_cross_process_flock" << std::endl;
}

int main() {
    test_acquire_release();
    test_same_key_serialized();
    test_different_key_parallel();
    test_cross_process_flock();
    std::cout << "All store_key_lock tests passed." << std::endl;
    return 0;
}
