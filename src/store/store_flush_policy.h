#pragma once

#include "store_types.h"

#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cgw {
namespace fw {
namespace store {

// Manages dirty/staged state for non-synchronous flush modes. Synchronous mode
// is a no-op policy (never dirty). Dirty entries are kept in a std::map so
// drain() yields a stable sorted key order (deterministic flush). The dirty
// budget (bytes + key count) bounds memory; overflow is reported so the Store
// can force a synchronous flush instead of silently dropping state.
class FlushPolicy {
public:
    FlushPolicy(FlushMode mode, std::size_t maxDirtyBytes,
                std::size_t maxDirtyKeys);

    bool isSynchronous() const { return m_mode == FlushMode::Synchronous; }
    FlushMode mode() const { return m_mode; }

    // Stage bytes for `key` (coalesces repeated writes). Returns true when the
    // dirty budget is exceeded and a forced flush is required.
    bool markDirty(std::string_view key, std::vector<uint8_t> bytes);

    bool isDirty(std::string_view key) const;

    // Read-through: staged bytes for a dirty key (empty if not dirty).
    std::vector<uint8_t> staged(std::string_view key) const;

    // Drain all dirty entries in stable (sorted key) order; clears staging.
    std::vector<std::pair<std::string, std::vector<uint8_t>>> drain();

    // Drop a single key from staging (after a successful commit).
    void clear(std::string_view key);

    std::size_t dirtyBytes() const;
    std::size_t dirtyCount() const;
    bool needsForcedFlush() const;

private:
    FlushMode m_mode;
    std::size_t m_maxDirtyBytes;
    std::size_t m_maxDirtyKeys;
    mutable std::mutex m_mutex;
    std::map<std::string, std::vector<uint8_t>> m_dirty;
    std::size_t m_dirtyBytes = 0;
};

} // namespace store
} // namespace fw
} // namespace cgw
