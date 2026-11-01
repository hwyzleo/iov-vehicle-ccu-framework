#include "store_file_reader.h"
#include "store_error_formatter.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
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

} // namespace

std::vector<uint8_t>
FileReader::read(const fs::path& path, std::size_t maxValueBytes,
                 std::string_view service, std::string_view key) {
    int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) {
            fail("CCU-FW-0105", "final file not found", service, key,
                 path.string(), "read");
        }
        if (errno == ELOOP) {
            fail("CCU-FW-0101", "final file is a symlink (escape prevention)",
                 service, key, path.string(), "read");
        }
        fail("CCU-FW-0102",
             std::string("cannot open final file: ") + ::strerror(errno),
             service, key, path.string(), "read");
    }

    struct stat st;
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        fail("CCU-FW-0102", "cannot stat final file", service, key,
             path.string(), "read");
    }
    if (!S_ISREG(st.st_mode)) {
        ::close(fd);
        fail("CCU-FW-0101", "final path is not a regular file", service, key,
             path.string(), "read");
    }
    if (static_cast<std::size_t>(st.st_size) > maxValueBytes) {
        ::close(fd);
        fail("CCU-FW-0104", "final file exceeds size limit", service, key,
             path.string(), "read");
    }

    std::vector<uint8_t> buf(static_cast<std::size_t>(st.st_size));
    std::size_t off = 0;
    while (off < buf.size()) {
        ssize_t n = ::read(fd, buf.data() + off, buf.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            fail("CCU-FW-0102",
                 std::string("read failed: ") + ::strerror(errno), service, key,
                 path.string(), "read");
        }
        if (n == 0) break; // unexpected EOF; truncated file
        off += static_cast<std::size_t>(n);
    }
    ::close(fd);
    buf.resize(off);
    return buf;
}

} // namespace store
} // namespace fw
} // namespace ccu
