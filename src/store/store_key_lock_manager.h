#pragma once

#include "store_types.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unistd.h>

namespace ccu {
namespace fw {
namespace store {

namespace fs = std::filesystem;

// RAII handle holding both the in-process per-key mutex and the cross-process
// flock. Destruction releases flock (close fd); the in-process guard releases
// its mutex. Lock order is fixed: in-process key mutex -> cross-process key
// lock -> file operations (never reversed).
class LockHandle {
public:
    LockHandle(std::shared_ptr<std::timed_mutex> m,
               std::unique_lock<std::timed_mutex> g, int fd, fs::path lockPath)
        : m_mutex(std::move(m))
        , m_guard(std::move(g))
        , m_flockFd(fd)
        , m_lockPath(std::move(lockPath)) {}
    ~LockHandle() {
        if (m_flockFd >= 0) ::close(m_flockFd);
    }
    LockHandle(const LockHandle&) = delete;
    LockHandle& operator=(const LockHandle&) = delete;
private:
    std::shared_ptr<std::timed_mutex> m_mutex;
    std::unique_lock<std::timed_mutex> m_guard;
    int m_flockFd;
    fs::path m_lockPath;
};

// Per-key lock manager. In-process mutexes are shared across Store instances
// for the same (serviceDir, key) via a process-global registry, so two Store
// instances of the same service cannot bypass each other. Cross-process
// serialization uses flock on <serviceDir>/.<key>.lock.
class KeyLockManager {
public:
    KeyLockManager(fs::path serviceDir, std::chrono::milliseconds timeout);
    // Acquire in-process mutex then cross-process flock within the timeout.
    // Throws CCU-FW-0103 on failure/timeout.
    std::unique_ptr<LockHandle> acquire(std::string_view key,
                                        std::string_view service);
private:
    fs::path m_serviceDir;
    std::chrono::milliseconds m_timeout;
};

} // namespace store
} // namespace fw
} // namespace ccu
