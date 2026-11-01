#include "store_key_lock_manager.h"
#include "store_path_resolver.h"
#include "store_error_formatter.h"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <map>
#include <system_error>
#include <thread>

namespace ccu {
namespace fw {
namespace store {

namespace fs = std::filesystem;

namespace {

void failLock(std::string_view reason, std::string_view service,
              std::string_view key, std::string_view path) {
    throw StoreException(
        "CCU-FW-0103",
        StoreErrorFormatter::format("CCU-FW-0103", reason, service, key, path,
                                    "lock"),
        std::string(service), std::string(key), std::string(path), "lock");
}

// Process-global registry of per-(serviceDir,key) timed mutexes. Sharing across
// Store instances ensures same-process instances of the same service cannot
// bypass each other's in-process locks.
struct InProcRegistry {
    std::mutex mu;
    std::map<std::string, std::shared_ptr<std::timed_mutex>> map;
};

InProcRegistry& inProcRegistry() {
    static InProcRegistry r;
    return r;
}

std::string registryKey(const fs::path& serviceDir, std::string_view key) {
    return serviceDir.string() + "\x1f" + std::string(key);
}

} // namespace

KeyLockManager::KeyLockManager(fs::path serviceDir,
                               std::chrono::milliseconds timeout)
    : m_serviceDir(std::move(serviceDir))
    , m_timeout(timeout) {}

std::unique_ptr<LockHandle> KeyLockManager::acquire(std::string_view key,
                                                    std::string_view service) {
    // 1. In-process per-key mutex (shared via global registry).
    std::shared_ptr<std::timed_mutex> mtx;
    {
        auto& reg = inProcRegistry();
        std::lock_guard<std::mutex> lk(reg.mu);
        auto id = registryKey(m_serviceDir, key);
        auto it = reg.map.find(id);
        if (it == reg.map.end()) {
            mtx = std::make_shared<std::timed_mutex>();
            reg.map.emplace(std::move(id), mtx);
        } else {
            mtx = it->second;
        }
    }

    std::unique_lock<std::timed_mutex> guard(*mtx, std::defer_lock);
    auto deadline = std::chrono::steady_clock::now() + m_timeout;
    while (true) {
        if (guard.try_lock()) break;
        if (std::chrono::steady_clock::now() >= deadline) {
            failLock("in-process lock timeout", service, key, "");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // 2. Cross-process flock on <serviceDir>/.<key>.lock.
    fs::path lockPath = PathResolver::lockFile(m_serviceDir, key);
    int fd = ::open(lockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0) {
        failLock(std::string("cannot open lock file: ") + ::strerror(errno),
                 service, key, lockPath.string());
    }

    auto flockDeadline = std::chrono::steady_clock::now() + m_timeout;
    while (true) {
        if (::flock(fd, LOCK_EX | LOCK_NB) == 0) {
            return std::make_unique<LockHandle>(mtx, std::move(guard), fd,
                                                lockPath);
        }
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            ::close(fd);
            failLock(std::string("flock failed: ") + ::strerror(errno), service,
                     key, lockPath.string());
        }
        if (std::chrono::steady_clock::now() >= flockDeadline) {
            ::close(fd);
            failLock("cross-process lock timeout", service, key,
                     lockPath.string());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

} // namespace store
} // namespace fw
} // namespace ccu
