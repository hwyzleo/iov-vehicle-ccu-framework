#include "store_atomic_writer.h"
#include "store_error_formatter.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace ccu {
namespace fw {
namespace store {

namespace {

void fail(std::string_view code, std::string_view reason,
          std::string_view service, std::string_view key, std::string_view path,
          std::string_view stage) {
    throw StoreException(
        std::string(code),
        StoreErrorFormatter::format(code, reason, service, key, path, stage),
        std::string(service), std::string(key), std::string(path),
        std::string(stage));
}

std::string tempName(std::string_view key) {
    static std::atomic<unsigned long long> g_nonce{0};
    unsigned long long n = ++g_nonce;
    // Mix pid (cross-process uniqueness) with the in-process counter.
    unsigned long long rnd =
        static_cast<unsigned long long>(::getpid()) * 100003ull + n;
    char buf[128];
    std::snprintf(buf, sizeof(buf), ".%.*s.%d.%llu.tmp",
                  static_cast<int>(key.size()), key.data(),
                  static_cast<int>(::getpid()),
                  static_cast<unsigned long long>(rnd));
    return buf;
}

// Write all bytes, handling short writes and EINTR. Returns false on error.
bool writeAll(int fd, const uint8_t* data, std::size_t len) {
    std::size_t off = 0;
    while (off < len) {
        ssize_t n = ::write(fd, data + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        off += static_cast<std::size_t>(n);
    }
    return true;
}

} // namespace

void AtomicWriter::syncDir(const fs::path& dir, std::string_view service,
                           std::string_view key, std::string_view stage) {
    int dfd = ::open(dir.c_str(), O_RDONLY | O_CLOEXEC);
    if (dfd < 0) {
        fail("CCU-FW-0102",
             std::string("cannot open service dir for fsync: ") + ::strerror(errno),
             service, key, dir.string(), stage);
    }
    if (::fsync(dfd) != 0) {
        ::close(dfd);
        fail("CCU-FW-0102",
             std::string("directory fsync failed: ") + ::strerror(errno), service,
             key, dir.string(), stage);
    }
    ::close(dfd);
}

void AtomicWriter::commit(const fs::path& finalPath, const fs::path& serviceDir,
                          std::string_view service, std::string_view key,
                          const std::vector<uint8_t>& bytes,
                          std::size_t maxValueBytes) {
    if (bytes.size() > maxValueBytes) {
        fail("CCU-FW-0104", "encoded value exceeds size limit", service, key,
             finalPath.string(), "encode");
    }

    fs::path tempPath = serviceDir / tempName(key);
    std::string tempStr = tempPath.string();

    int fd = -1;
    // Retry a few times in the (unlikely) case of a temp name collision.
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (attempt > 0) {
            tempStr = (serviceDir / tempName(key)).string();
            tempPath = tempStr;
        }
        fd = ::open(tempStr.c_str(),
                    O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd >= 0) break;
        if (errno != EEXIST) {
            fail("CCU-FW-0102",
                 std::string("cannot create temp file: ") + ::strerror(errno),
                 service, key, tempStr, "temp-create");
        }
    }
    if (fd < 0) {
        fail("CCU-FW-0102", "temp file collision exhausted", service, key,
             tempStr, "temp-create");
    }

    // Cleanup helper for pre-rename failures.
    auto cleanup = [&]() {
        if (fd >= 0) ::close(fd);
        std::error_code ec;
        fs::remove(tempPath, ec);
    };

    if (!writeAll(fd, bytes.data(), bytes.size())) {
        cleanup();
        fail("CCU-FW-0102",
             std::string("write failed: ") + ::strerror(errno), service, key,
             tempStr, "write");
    }

    if (::fsync(fd) != 0) {
        cleanup();
        fail("CCU-FW-0102",
             std::string("temp fsync failed: ") + ::strerror(errno), service,
             key, tempStr, "fsync");
    }

    if (::close(fd) != 0) {
        fd = -1;
        cleanup();
        fail("CCU-FW-0102",
             std::string("temp close failed: ") + ::strerror(errno), service,
             key, tempStr, "fsync");
    }
    fd = -1;

    // rename is atomic on the same filesystem.
    if (::rename(tempStr.c_str(), finalPath.c_str()) != 0) {
        cleanup();
        fail("CCU-FW-0102",
             std::string("rename failed: ") + ::strerror(errno), service, key,
             finalPath.string(), "rename");
    }

    // New value is now visible in memory. If dir fsync fails, durability is
    // unconfirmed: report CCU-FW-0102 with stage "dir-fsync" (do NOT unlink).
    syncDir(serviceDir, service, key, "dir-fsync");
}

} // namespace store
} // namespace fw
} // namespace ccu
