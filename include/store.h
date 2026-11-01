#pragma once

#include "store_types.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace ccu {
namespace fw {
namespace store {

// ============================================================
// Store - versioned, atomic, crash-safe local persistence facade
// ============================================================
// Independent in-process C++ library; does not depend on config/log/SOME-IP.
// The host service reads common.store.root from a ConfigSnapshot and injects
// it via StoreOptions::root. Default flushMode is Synchronous (durable on
// return). Non-synchronous modes stage state in memory and require an explicit
// flush(); isDirty() reflects uncommitted state.
class Store {
public:
    // Validate service/key rules, normalize root, create the 0700 service
    // directory. Throws StoreException(CCU-FW-0101) on path/permission failure.
    static Store open(std::string_view service, const StoreOptions& options = {});

    // Encode value with the registered Serializer and persist atomically.
    // Synchronous mode: durable (temp -> fsync -> rename -> dir fsync) on
    // return. Non-synchronous modes: stage in memory + mark dirty.
    template <class T>
    void save(std::string_view key, const T& value) {
        detail::saveBytes(*m_impl, key,
                          detail::SerializerDispatch<T>::encode(value));
    }

    // Load and decode. Throws CCU-FW-0105 if missing, CCU-FW-0104 on
    // decode/type-mismatch/size, CCU-FW-0101/0102 on path/I/O.
    template <class T>
    T load(std::string_view key) const {
        return detail::SerializerDispatch<T>::decode(detail::loadBytes(*m_impl, key));
    }

    // Returns defaultValue only when the key is missing (CCU-FW-0105).
    // CCU-FW-0101~0104 are never swallowed.
    template <class T>
    T loadOr(std::string_view key, T defaultValue) const {
        try {
            return load<T>(key);
        } catch (const StoreException& e) {
            if (e.code == "CCU-FW-0105") return defaultValue;
            throw;
        }
    }

    // String-literal default convenience.
    std::string loadOr(std::string_view key, const char* defaultValue) const {
        try {
            return load<std::string>(key);
        } catch (const StoreException& e) {
            if (e.code == "CCU-FW-0105") return std::string(defaultValue);
            throw;
        }
    }

    // True only when the normalized final file exists and is a regular file
    // (or the key has staged dirty state in non-synchronous modes).
    // Permission/I/O errors are thrown, not reported as false.
    bool has(std::string_view key) const { return detail::hasBytes(*m_impl, key); }

    // Remove the final file under the per-key lock and fsync the service dir.
    // Idempotent for missing keys (returns false if nothing was removed).
    bool remove(std::string_view key) { return detail::removeBytes(*m_impl, key); }

    // Synchronous mode: idempotent no-op (confirms no dirty keys).
    // Non-synchronous modes: commit all dirty keys in stable order; partial
    // failure leaves remaining keys dirty and throws.
    void flush() { detail::flushImpl(*m_impl); }

    // True if `key` has uncommitted staged state (non-synchronous modes only).
    bool isDirty(std::string_view key) const { return detail::isDirtyImpl(*m_impl, key); }

private:
    std::shared_ptr<detail::StoreImpl> m_impl;
    explicit Store(std::shared_ptr<detail::StoreImpl> impl) : m_impl(std::move(impl)) {}
};

} // namespace store
} // namespace fw
} // namespace ccu
