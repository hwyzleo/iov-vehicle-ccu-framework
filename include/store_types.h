#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ccu {
namespace fw {
namespace store {

// ============================================================
// FlushMode
// ============================================================
// Synchronous (default): save() is durable on return (temp -> fsync ->
//   rename -> dir fsync); flush() is an idempotent no-op; isDirty() is false.
// Deferred / Debounce / Batch: save() stages a controlled in-memory copy and
//   marks the key dirty; the value is NOT durable until flush(). Debounce
//   coalesces repeated writes to the same key; Batch commits all dirty keys in
//   one flush(). Dirty state is bounded by maxDirtyBytes/maxDirtyKeys; budget
//   overflow forces a synchronous flush rather than silently dropping state.
enum class FlushMode {
    Synchronous,
    Deferred,
    Debounce,
    Batch,
};

// ============================================================
// StoreOptions
// ============================================================
struct StoreOptions {
    std::filesystem::path root{"/var/lib/ccu"};
    std::chrono::milliseconds lockTimeout{3000};
    std::size_t maxValueBytes{4 * 1024 * 1024};
    FlushMode flushMode{FlushMode::Synchronous};
    // Non-synchronous dirty budget. Overflow forces a synchronous flush.
    std::size_t maxDirtyBytes{16 * 1024 * 1024};
    std::size_t maxDirtyKeys{4096};
    // Debounce coalesce window (Debounce only). <=0 disables the auto timer;
    // the host service calls flush() explicitly.
    std::chrono::milliseconds debounceInterval{0};
};

// ============================================================
// StoreException (CCU-FW-0101~0105)
// ============================================================
// code: CCU-FW-0101 root/dir/permission/path-safety check failed (fail-closed)
//       CCU-FW-0102 create/write/fsync/rename/unlink/dir-sync failed
//       CCU-FW-0103 in-process/cross-process lock failed or timed out
//       CCU-FW-0104 encode/decode/format-version/size illegal
//       CCU-FW-0105 final file missing
// stage: logical step (e.g. "validate", "encode", "lock", "temp-create",
//        "write", "fsync", "rename", "dir-fsync", "read", "decode", "remove")
// path/service/key carry a minimal path summary; SHALL NOT contain state
// contents, tokens, certs or key material.
struct StoreException : std::runtime_error {
    std::string code;
    std::string service;
    std::string key;
    std::string path;
    std::string stage;

    StoreException(std::string code_, const std::string& message,
                   std::string service_ = "", std::string key_ = "",
                   std::string path_ = "", std::string stage_ = "")
        : std::runtime_error(message)
        , code(std::move(code_))
        , service(std::move(service_))
        , key(std::move(key_))
        , path(std::move(path_))
        , stage(std::move(stage_)) {}
};

// ============================================================
// Serialization dispatch (versioned, PRIVATE binary framing)
// ============================================================
// Public header exposes only typed entry points; the on-disk framing (magic,
// format version, type tag, byte order, length prefixes) is an implementation
// detail in the .cpp. load<T> verifies the stored type tag matches T, so a
// type mismatch yields CCU-FW-0104 rather than a corrupted object. Unserved T
// (no SerializerDispatch specialization) yields a compile error, preventing
// raw memory dumps of unknown ABI objects.
namespace detail {

class StoreImpl;

// Typed encode overloads (defined in store_serializer.cpp).
std::vector<uint8_t> encode(const std::string& v);
std::vector<uint8_t> encode(int64_t v);
std::vector<uint8_t> encode(double v);
std::vector<uint8_t> encode(bool v);
std::vector<uint8_t> encode(const std::vector<std::string>& v);
std::vector<uint8_t> encode(const std::vector<int64_t>& v);

// Typed decode; each verifies the stored type tag (defined in store_serializer.cpp).
std::string              decodeString(const std::vector<uint8_t>& b);
int64_t                  decodeInt64(const std::vector<uint8_t>& b);
double                   decodeDouble(const std::vector<uint8_t>& b);
bool                     decodeBool(const std::vector<uint8_t>& b);
std::vector<std::string> decodeStringVec(const std::vector<uint8_t>& b);
std::vector<int64_t>     decodeInt64Vec(const std::vector<uint8_t>& b);

// Compile-time dispatch table. Add a specialization to support a new type.
template <class T> struct SerializerDispatch;
template <> struct SerializerDispatch<std::string> {
    static std::vector<uint8_t> encode(const std::string& v) { return detail::encode(v); }
    static std::string decode(const std::vector<uint8_t>& b) { return detail::decodeString(b); }
};
template <> struct SerializerDispatch<int64_t> {
    static std::vector<uint8_t> encode(int64_t v) { return detail::encode(v); }
    static int64_t decode(const std::vector<uint8_t>& b) { return detail::decodeInt64(b); }
};
template <> struct SerializerDispatch<double> {
    static std::vector<uint8_t> encode(double v) { return detail::encode(v); }
    static double decode(const std::vector<uint8_t>& b) { return detail::decodeDouble(b); }
};
template <> struct SerializerDispatch<bool> {
    static std::vector<uint8_t> encode(bool v) { return detail::encode(v); }
    static bool decode(const std::vector<uint8_t>& b) { return detail::decodeBool(b); }
};
template <> struct SerializerDispatch<std::vector<std::string>> {
    static std::vector<uint8_t> encode(const std::vector<std::string>& v) { return detail::encode(v); }
    static std::vector<std::string> decode(const std::vector<uint8_t>& b) { return detail::decodeStringVec(b); }
};
template <> struct SerializerDispatch<std::vector<int64_t>> {
    static std::vector<uint8_t> encode(const std::vector<int64_t>& v) { return detail::encode(v); }
    static std::vector<int64_t> decode(const std::vector<uint8_t>& b) { return detail::decodeInt64Vec(b); }
};

// Non-template core operations on StoreImpl (defined in store_store.cpp).
void saveBytes(StoreImpl& impl, std::string_view key, std::vector<uint8_t> bytes);
std::vector<uint8_t> loadBytes(const StoreImpl& impl, std::string_view key);
bool hasBytes(const StoreImpl& impl, std::string_view key);
bool removeBytes(StoreImpl& impl, std::string_view key);
void flushImpl(StoreImpl& impl);
bool isDirtyImpl(const StoreImpl& impl, std::string_view key);

} // namespace detail

} // namespace store
} // namespace fw
} // namespace ccu
