#pragma once

#include "store_types.h"
#include "store_path_resolver.h"
#include "store_key_lock_manager.h"
#include "store_flush_policy.h"

#include <filesystem>
#include <memory>
#include <string>

namespace ccu {
namespace fw {
namespace store {

namespace fs = std::filesystem;

namespace detail {

// Concrete implementation state held behind the Store facade (PImpl). The
// public headers only forward-declare this (detail::StoreImpl); the complete
// type lives here in the PRIVATE source tree.
struct StoreImpl {
    std::string service;
    StoreOptions options;
    fs::path serviceDir;
    std::unique_ptr<KeyLockManager> locks;
    std::unique_ptr<FlushPolicy> flush;
};

} // namespace detail

} // namespace store
} // namespace fw
} // namespace ccu
