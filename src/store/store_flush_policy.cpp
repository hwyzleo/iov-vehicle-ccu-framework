#include "store_flush_policy.h"

namespace ccu {
namespace fw {
namespace store {

FlushPolicy::FlushPolicy(FlushMode mode, std::size_t maxDirtyBytes,
                         std::size_t maxDirtyKeys)
    : m_mode(mode)
    , m_maxDirtyBytes(maxDirtyBytes)
    , m_maxDirtyKeys(maxDirtyKeys) {}

bool FlushPolicy::markDirty(std::string_view key, std::vector<uint8_t> bytes) {
    if (m_mode == FlushMode::Synchronous) return false;
    std::lock_guard<std::mutex> lk(m_mutex);
    std::string k(key);
    auto it = m_dirty.find(k);
    if (it != m_dirty.end()) {
        m_dirtyBytes -= it->second.size();
        it->second = std::move(bytes);
    } else {
        m_dirty.emplace(std::move(k), std::move(bytes));
    }
    auto now = m_dirty.find(std::string(key));
    if (now != m_dirty.end()) m_dirtyBytes += now->second.size();
    return m_dirtyBytes > m_maxDirtyBytes || m_dirty.size() > m_maxDirtyKeys;
}

bool FlushPolicy::isDirty(std::string_view key) const {
    if (m_mode == FlushMode::Synchronous) return false;
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_dirty.count(std::string(key)) != 0;
}

std::vector<uint8_t> FlushPolicy::staged(std::string_view key) const {
    if (m_mode == FlushMode::Synchronous) return {};
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_dirty.find(std::string(key));
    if (it == m_dirty.end()) return {};
    return it->second;
}

std::vector<std::pair<std::string, std::vector<uint8_t>>> FlushPolicy::drain() {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> out;
    if (m_mode == FlushMode::Synchronous) return out;
    std::lock_guard<std::mutex> lk(m_mutex);
    out.reserve(m_dirty.size());
    for (auto& [k, v] : m_dirty) {
        out.emplace_back(k, std::move(v));
    }
    m_dirty.clear();
    m_dirtyBytes = 0;
    return out;
}

void FlushPolicy::clear(std::string_view key) {
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_dirty.find(std::string(key));
    if (it != m_dirty.end()) {
        m_dirtyBytes -= it->second.size();
        m_dirty.erase(it);
    }
}

std::size_t FlushPolicy::dirtyBytes() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_dirtyBytes;
}

std::size_t FlushPolicy::dirtyCount() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_dirty.size();
}

bool FlushPolicy::needsForcedFlush() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_dirtyBytes > m_maxDirtyBytes || m_dirty.size() > m_maxDirtyKeys;
}

} // namespace store
} // namespace fw
} // namespace ccu
