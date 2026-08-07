#include "store/store_flush_policy.h"
#include "store.h"
#include "store_types.h"
#include "store_test_util.h"

#include <cassert>
#include <fstream>
#include <iostream>

using namespace cgw::fw::store;
using cgwtest::TempDir;
namespace fs = std::filesystem;

// ---- FlushPolicy unit tests ----

void test_policy_sync_no_dirty() {
    FlushPolicy p(FlushMode::Synchronous, 1024, 16);
    assert(p.isSynchronous());
    assert(!p.markDirty("k", {1, 2, 3}));
    assert(!p.isDirty("k"));
    assert(p.drain().empty());
    std::cout << "  [PASS] test_policy_sync_no_dirty" << std::endl;
}

void test_policy_mark_dirty_and_drain_sorted() {
    FlushPolicy p(FlushMode::Deferred, 1024, 16);
    p.markDirty("banana", {'b'});
    p.markDirty("apple", {'a'});
    p.markDirty("cherry", {'c'});
    auto d = p.drain();
    assert(d.size() == 3);
    assert(d[0].first == "apple");
    assert(d[1].first == "banana");
    assert(d[2].first == "cherry");
    assert(!p.isDirty("apple"));
    assert(p.dirtyCount() == 0);
    std::cout << "  [PASS] test_policy_mark_dirty_and_drain_sorted" << std::endl;
}

void test_policy_coalesce_same_key() {
    FlushPolicy p(FlushMode::Deferred, 1024, 16);
    p.markDirty("k", {'a'});
    p.markDirty("k", {'b'});
    p.markDirty("k", {'c'});
    assert(p.dirtyCount() == 1);
    auto d = p.drain();
    assert(d.size() == 1);
    assert(d[0].second == std::vector<uint8_t>{'c'});
    std::cout << "  [PASS] test_policy_coalesce_same_key" << std::endl;
}

void test_policy_budget_overflow() {
    FlushPolicy p(FlushMode::Deferred, 10, 16);
    std::vector<uint8_t> big(100, 'x');
    bool force = p.markDirty("k", big);
    assert(force); // exceeds maxDirtyBytes(10)
    assert(p.needsForcedFlush());
    std::cout << "  [PASS] test_policy_budget_overflow" << std::endl;
}

void test_policy_clear() {
    FlushPolicy p(FlushMode::Deferred, 1024, 16);
    p.markDirty("k", {'a'});
    assert(p.isDirty("k"));
    p.clear("k");
    assert(!p.isDirty("k"));
    std::cout << "  [PASS] test_policy_clear" << std::endl;
}

void test_policy_staged_readthrough() {
    FlushPolicy p(FlushMode::Deferred, 1024, 16);
    p.markDirty("k", {'v', '1'});
    auto s = p.staged("k");
    assert((s == std::vector<uint8_t>{'v', '1'}));
    assert(p.staged("missing").empty());
    std::cout << "  [PASS] test_policy_staged_readthrough" << std::endl;
}

// ---- Store deferred-mode tests ----

static Store openStore(const fs::path& root, FlushMode mode) {
    StoreOptions opt;
    opt.root = root;
    opt.flushMode = mode;
    return Store::open("diag", opt);
}

void test_deferred_dirty_then_flush() {
    TempDir tmp;
    Store s = openStore(tmp.path, FlushMode::Deferred);
    s.save<int64_t>("counter", 5);
    assert(s.isDirty("counter"));
    // Read-through returns staged value before flush.
    assert(s.load<int64_t>("counter") == 5);
    s.flush();
    assert(!s.isDirty("counter"));
    assert(s.load<int64_t>("counter") == 5);
    std::cout << "  [PASS] test_deferred_dirty_then_flush" << std::endl;
}

void test_deferred_not_durable_without_flush() {
    TempDir tmp;
    {
        Store s = openStore(tmp.path, FlushMode::Deferred);
        s.save<int64_t>("counter", 9);
        assert(s.isDirty("counter"));
        // No flush; staged state is lost when Store is destroyed.
    }
    Store s2 = openStore(tmp.path, FlushMode::Deferred);
    bool threw = false;
    try { s2.load<int64_t>("counter"); }
    catch (const StoreException& e) { threw = (e.code == "CGW-FW-0105"); }
    assert(threw);
    std::cout << "  [PASS] test_deferred_not_durable_without_flush" << std::endl;
}

void test_debounce_coalesce() {
    TempDir tmp;
    Store s = openStore(tmp.path, FlushMode::Debounce);
    s.save<int64_t>("k", 1);
    s.save<int64_t>("k", 2);
    s.save<int64_t>("k", 3);
    assert(s.isDirty("k"));
    s.flush();
    assert(!s.isDirty("k"));
    assert(s.load<int64_t>("k") == 3); // last write wins
    std::cout << "  [PASS] test_debounce_coalesce" << std::endl;
}

void test_batch_flush_multiple_keys() {
    TempDir tmp;
    Store s = openStore(tmp.path, FlushMode::Batch);
    s.save<int64_t>("a", 1);
    s.save<int64_t>("b", 2);
    s.save<int64_t>("c", 3);
    assert(s.isDirty("a") && s.isDirty("b") && s.isDirty("c"));
    s.flush();
    assert(!s.isDirty("a") && !s.isDirty("b") && !s.isDirty("c"));
    assert(s.load<int64_t>("a") == 1);
    assert(s.load<int64_t>("b") == 2);
    assert(s.load<int64_t>("c") == 3);
    std::cout << "  [PASS] test_batch_flush_multiple_keys" << std::endl;
}

void test_flush_failure_restages() {
    TempDir tmp;
    Store s = openStore(tmp.path, FlushMode::Deferred);
    s.save<int64_t>("k", 7);
    // Pre-create the lock file so a read-only dir still lets acquire() open it.
    fs::path lockP = tmp.path / "diag" / ".k.lock";
    { std::ofstream f(lockP, std::ios::binary); }
    // Make the service dir read-only -> temp creation fails -> 0102.
    fs::permissions(tmp.path / "diag",
                    fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace);
    bool threw = false;
    try { s.flush(); }
    catch (const StoreException& e) { threw = (e.code == "CGW-FW-0102"); }
    assert(threw);
    // Key remains dirty (re-staged).
    assert(s.isDirty("k"));
    // Restore permissions and flush again -> success.
    fs::permissions(tmp.path / "diag", fs::perms::owner_all,
                    fs::perm_options::replace);
    s.flush();
    assert(!s.isDirty("k"));
    assert(s.load<int64_t>("k") == 7);
    std::cout << "  [PASS] test_flush_failure_restages" << std::endl;
}

void test_deferred_budget_overflow_forces_flush() {
    TempDir tmp;
    StoreOptions opt;
    opt.root = tmp.path;
    opt.flushMode = FlushMode::Deferred;
    opt.maxDirtyBytes = 8; // tiny budget
    Store s = Store::open("diag", opt);
    // A large value exceeds the budget -> save forces a synchronous flush.
    s.save<std::string>("k", std::string(100, 'x'));
    // After forced flush, not dirty and durable.
    assert(!s.isDirty("k"));
    assert(s.load<std::string>("k") == std::string(100, 'x'));
    std::cout << "  [PASS] test_deferred_budget_overflow_forces_flush" << std::endl;
}

int main() {
    test_policy_sync_no_dirty();
    test_policy_mark_dirty_and_drain_sorted();
    test_policy_coalesce_same_key();
    test_policy_budget_overflow();
    test_policy_clear();
    test_policy_staged_readthrough();
    test_deferred_dirty_then_flush();
    test_deferred_not_durable_without_flush();
    test_debounce_coalesce();
    test_batch_flush_multiple_keys();
    test_flush_failure_restages();
    test_deferred_budget_overflow_forces_flush();
    std::cout << "All store_flush_policy tests passed." << std::endl;
    return 0;
}
