#include "store.h"
#include "store_impl.h"
#include "store_path_resolver.h"
#include "store_file_reader.h"
#include "store_atomic_writer.h"
#include "store_error_formatter.h"

#include <cerrno>
#include <cstring>
#include <system_error>
#include <unistd.h>

namespace cgw {
namespace fw {
namespace store {

namespace fs = std::filesystem;

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

void validateKey(const detail::StoreImpl& impl, std::string_view key) {
    if (!PathResolver::isValidKey(key)) {
        fail("CGW-FW-0101", "invalid key", impl.service, key, "", "validate");
    }
}

} // namespace

Store Store::open(std::string_view service, const StoreOptions& options) {
    if (!PathResolver::isValidServiceName(service)) {
        fail("CGW-FW-0101", "invalid service name", service, "", "", "validate");
    }
    fs::path root = PathResolver::normalizeRoot(options.root);
    PathResolver::ensureServiceDir(root, service, service);

    auto impl = std::make_shared<detail::StoreImpl>();
    impl->service = std::string(service);
    impl->options = options;
    impl->options.root = root;
    impl->serviceDir = PathResolver::serviceDir(root, service);
    impl->locks = std::make_unique<KeyLockManager>(impl->serviceDir,
                                                   options.lockTimeout);
    impl->flush = std::make_unique<FlushPolicy>(
        options.flushMode, options.maxDirtyBytes, options.maxDirtyKeys);
    return Store(std::move(impl));
}

namespace detail {

void saveBytes(StoreImpl& impl, std::string_view key,
               std::vector<uint8_t> bytes) {
    validateKey(impl, key);
    if (bytes.size() > impl.options.maxValueBytes) {
        fail("CGW-FW-0104", "encoded value exceeds size limit", impl.service,
             key, "", "encode");
    }
    fs::path finalP = PathResolver::finalFile(impl.serviceDir, key);

    if (impl.flush->isSynchronous()) {
        auto lock = impl.locks->acquire(key, impl.service);
        AtomicWriter::commit(finalP, impl.serviceDir, impl.service, key, bytes,
                             impl.options.maxValueBytes);
    } else {
        bool force = impl.flush->markDirty(key, std::move(bytes));
        if (force) flushImpl(impl); // budget overflow -> synchronous commit
    }
}

std::vector<uint8_t> loadBytes(const StoreImpl& impl, std::string_view key) {
    validateKey(impl, key);
    // Read-through staging for non-synchronous modes.
    if (std::vector<uint8_t> s = impl.flush->staged(key); !s.empty()) {
        return s;
    }
    fs::path finalP = PathResolver::finalFile(impl.serviceDir, key);
    return FileReader::read(finalP, impl.options.maxValueBytes, impl.service,
                            key);
}

bool hasBytes(const StoreImpl& impl, std::string_view key) {
    validateKey(impl, key);
    if (impl.flush->isDirty(key)) return true;
    fs::path finalP = PathResolver::finalFile(impl.serviceDir, key);
    std::error_code ec;
    auto st = fs::symlink_status(finalP, ec);
    if (ec) {
        // Some platforms report "not found" as an error (ENOENT); treat that as
        // missing. Other errors are real I/O failures.
        if (ec == std::errc::no_such_file_or_directory) return false;
        fail("CGW-FW-0102", std::string("cannot stat final file: ") + ec.message(),
             impl.service, key, finalP.string(), "read");
    }
    if (st.type() == fs::file_type::not_found ||
        st.type() == fs::file_type::none) {
        return false;
    }
    if (st.type() != fs::file_type::regular) {
        fail("CGW-FW-0101", "final path is not a regular file", impl.service,
             key, finalP.string(), "read");
    }
    return true;
}

bool removeBytes(StoreImpl& impl, std::string_view key) {
    validateKey(impl, key);
    impl.flush->clear(key);

    fs::path finalP = PathResolver::finalFile(impl.serviceDir, key);
    auto lock = impl.locks->acquire(key, impl.service);

    std::error_code ec;
    auto st = fs::symlink_status(finalP, ec);
    if (ec) {
        if (ec == std::errc::no_such_file_or_directory) return false;
        fail("CGW-FW-0102",
             std::string("cannot stat final file: ") + ec.message(),
             impl.service, key, finalP.string(), "remove");
    }
    if (st.type() == fs::file_type::not_found ||
        st.type() == fs::file_type::none) {
        return false; // idempotent: nothing to remove
    }
    if (st.type() != fs::file_type::regular) {
        fail("CGW-FW-0101",
             "refuse to remove non-regular/symlink final file", impl.service,
             key, finalP.string(), "remove");
    }
    if (::unlink(finalP.c_str()) != 0) {
        if (errno == ENOENT) return false; // raced removal
        fail("CGW-FW-0102",
             std::string("unlink failed: ") + ::strerror(errno), impl.service,
             key, finalP.string(), "remove");
    }
    AtomicWriter::syncDir(impl.serviceDir, impl.service, key, "remove-dir-fsync");
    return true;
}

void flushImpl(StoreImpl& impl) {
    if (impl.flush->isSynchronous()) return; // no dirty keys by construction
    auto dirty = impl.flush->drain();
    for (std::size_t i = 0; i < dirty.size(); ++i) {
        const auto& k = dirty[i].first;
        auto& bytes = dirty[i].second;
        try {
            fs::path finalP = PathResolver::finalFile(impl.serviceDir, k);
            auto lock = impl.locks->acquire(k, impl.service);
            AtomicWriter::commit(finalP, impl.serviceDir, impl.service, k, bytes,
                                 impl.options.maxValueBytes);
        } catch (const StoreException&) {
            // Re-stage this key and all not-yet-committed keys so they remain
            // dirty, then propagate the first failure.
            for (std::size_t j = i; j < dirty.size(); ++j) {
                impl.flush->markDirty(dirty[j].first, std::move(dirty[j].second));
            }
            throw;
        }
    }
}

bool isDirtyImpl(const StoreImpl& impl, std::string_view key) {
    return impl.flush->isDirty(key);
}

} // namespace detail

} // namespace store
} // namespace fw
} // namespace cgw
