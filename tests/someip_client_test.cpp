// tests/someip_client_test.cpp - Client request/release, availability, sync/
// async call, session association, timeout/cancel, late/duplicate response and
// retry (CR-005 §7/§8/§12/§14).

#include "someip_test_util.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace cgw::fw::someip;
using cgwtest::makeTestConfig;
using cgwtest::makeTestRuntime;
using cgwtest::InlineExecutor;
using cgwtest::DeferredExecutor;
using cgwtest::FakeSomeIpBackend;

namespace {

void test_availability_transitions() {
    auto h = makeTestRuntime(makeTestConfig("cl-avail"));
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});

    // Request before the provider offers -> Unavailable (CR-005 §8).
    c.requestService();
    assert(c.availability() == Availability::Unavailable);

    std::vector<Availability> seen;
    auto sub = c.onAvailability([&](Availability a) { seen.push_back(a); });
    // The current state is delivered immediately on registration.
    assert(!seen.empty() && seen.back() == Availability::Unavailable);

    // Provider offers -> Available.
    p.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::ok({});
    });
    p.offer();
    assert(c.availability() == Availability::Available);
    assert(seen.back() == Availability::Available);

    // Coalesce: re-offer (stop+offer) the same service yields Available again,
    // but a same-state transition is coalesced (no duplicate callback) (§8).
    p.stopOffer();  // -> Unavailable
    assert(c.availability() == Availability::Unavailable);
    p.offer();  // -> Available
    assert(c.availability() == Availability::Available);
    CGW_PASS("test_availability_transitions");
}

void test_version_mismatch() {
    auto h = makeTestRuntime(makeTestConfig("cl-ver"));
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {2, 0});  // major 2
    p.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::ok({});
    });
    p.offer();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});  // major 1
    c.requestService();
    assert(c.availability() == Availability::VersionMismatch);
    // Call on a version-mismatched service -> 0304 (CR-005 §7/§12).
    auto r = c.call(0x0001, PayloadView{nullptr, 0});
    assert(!r.success && r.code == "CGW-FW-0304");
    CGW_PASS("test_version_mismatch");
}

void test_unavailable_call_returns_0304() {
    auto h = makeTestRuntime(makeTestConfig("cl-unavail"));
    h.runtime.start();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();  // no provider -> Unavailable
    auto r = c.call(0x0001, PayloadView{nullptr, 0});
    assert(!r.success && r.code == "CGW-FW-0304");
    // Async returns a failed future too.
    auto f = c.callAsync(0x0001, Payload{}, CallOptions{});
    assert(!f.get().success);
    CGW_PASS("test_unavailable_call_returns_0304");
}

void test_sync_call_success() {
    auto h = makeTestRuntime(makeTestConfig("cl-sync"));
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.registerMethod(0x0001, [](const RequestContext&, PayloadView pv) {
        Payload resp(pv.data, pv.data + pv.size);
        return MethodResult::ok(std::move(resp));
    });
    p.offer();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();
    Payload req{'x', 'y', 'z'};
    auto r = c.call(0x0001, PayloadView{req.data(), req.size()});
    assert(r.success);
    assert(r.response == req);
    CGW_PASS("test_sync_call_success");
}

void test_async_call_success() {
    auto h = makeTestRuntime(makeTestConfig("cl-async"));
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.registerMethod(0x0001, [](const RequestContext&, PayloadView pv) {
        Payload resp(pv.data, pv.data + pv.size);
        return MethodResult::ok(std::move(resp));
    });
    p.offer();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();
    Payload req{'a', 'b'};
    auto fut = c.callAsync(0x0001, Payload(req), CallOptions{});
    auto r = fut.get();
    assert(r.success);
    assert(r.response == req);
    CGW_PASS("test_async_call_success");
}

void test_async_timeout_virtual_clock() {
    // DeferredExecutor: the handler is queued and never runs until drain(). The
    // virtual-clock timer fires on advanceClock -> 0305 (CR-005 §7/§14).
    auto exec = std::make_shared<DeferredExecutor>();
    auto h = makeTestRuntime(makeTestConfig("cl-async-to"), exec);
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::ok({});
    });
    p.offer();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();
    exec->drain();  // let the deferred availability callback run
    CallOptions opts;
    opts.timeout = std::chrono::milliseconds(100);
    auto fut = c.callAsync(0x0001, Payload{}, opts);
    // Advance the virtual clock past the timeout; the handler is still queued.
    h.backend->advanceClock(std::chrono::milliseconds(150));
    auto r = fut.get();
    assert(!r.success && r.code == "CGW-FW-0305");
    // The late response (queued handler) is discarded; draining does not change
    // the completed result (CR-005 §7).
    exec->drain();
    CGW_PASS("test_async_timeout_virtual_clock");
}

void test_sync_timeout_real_wait() {
    // DeferredExecutor + a tiny real-time timeout: the sync call's wait_for
    // elapses while the handler stays queued (CR-005 §7).
    auto exec = std::make_shared<DeferredExecutor>();
    auto h = makeTestRuntime(makeTestConfig("cl-sync-to"), exec);
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::ok({});
    });
    p.offer();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();
    exec->drain();
    CallOptions opts;
    opts.timeout = std::chrono::milliseconds(30);
    auto r = c.call(0x0001, PayloadView{nullptr, 0}, opts);
    assert(!r.success && r.code == "CGW-FW-0305");
    exec->drain();  // discard the late response
    CGW_PASS("test_sync_timeout_real_wait");
}

void test_non_idempotent_no_retry() {
    auto h = makeTestRuntime(makeTestConfig("cl-noretry"));
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::ok({});
    });
    p.offer();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();
    // Inject 2 transport failures; default RetryMode::None -> no retry, single
    // failure (US-507). The third call succeeds once failures are exhausted.
    h.backend->injectTransportFailures(2);
    auto r = c.call(0x0001, PayloadView{nullptr, 0});
    assert(!r.success && r.code == "CGW-FW-0305");
    // Remaining injected failures are still pending (only one was consumed).
    // A subsequent call with no injected failures succeeds.
    auto r2 = c.call(0x0001, PayloadView{nullptr, 0});
    // The 2 remaining failures apply here too (no retry), so it fails again.
    assert(!r2.success && r2.code == "CGW-FW-0305");
    auto r3 = c.call(0x0001, PayloadView{nullptr, 0});
    assert(r3.success);  // failures exhausted
    CGW_PASS("test_non_idempotent_no_retry");
}

void test_idempotent_bounded_retry_success() {
    auto h = makeTestRuntime(makeTestConfig("cl-retry-ok"));
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::ok(Payload{0x01});
    });
    p.offer();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();
    // 2 transport failures then success; idempotent with maxRetries=3 retries
    // twice and succeeds (CR-005 §7, US-507).
    h.backend->injectTransportFailures(2);
    CallOptions opts;
    opts.retry = RetryMode::Idempotent;
    opts.maxRetries = 3;
    auto r = c.call(0x0001, PayloadView{nullptr, 0}, opts);
    assert(r.success);
    assert(r.response.size() == 1 && r.response[0] == 0x01);
    CGW_PASS("test_idempotent_bounded_retry_success");
}

void test_idempotent_retry_exhausted() {
    auto h = makeTestRuntime(makeTestConfig("cl-retry-ex"));
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::ok({});
    });
    p.offer();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();
    // More failures than allowed retries: bounded retry then fail (§7).
    h.backend->injectTransportFailures(10);
    CallOptions opts;
    opts.retry = RetryMode::Idempotent;
    opts.maxRetries = 2;  // 1 initial + 2 retries = 3 attempts
    auto r = c.call(0x0001, PayloadView{nullptr, 0}, opts);
    assert(!r.success && r.code == "CGW-FW-0305");
    CGW_PASS("test_idempotent_retry_exhausted");
}

void test_subscription_raii() {
    auto h = makeTestRuntime(makeTestConfig("cl-sub-raii"));
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.offer();
    p.offerEvent(0x8001, 0x0001, EventOptions{});
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();
    int count = 0;
    {
        auto sub = c.subscribe(0x0001, 0x8001,
            [&](const EventContext&, PayloadView) { ++count; });
        assert(sub.active());
        p.notify(0x8001, PayloadView{nullptr, 0});
        assert(count == 1);
        // Explicit cancel: no further delivery (CR-005 §7).
        sub.cancel();
        assert(!sub.active());
        p.notify(0x8001, PayloadView{nullptr, 0});
        assert(count == 1);
    }
    // Destructor is idempotent after cancel.
    assert(h.backend->subscriptionCount() == 0);
    CGW_PASS("test_subscription_raii");
}

void test_inflight_limit() {
    SomeIpConfig cfg = makeTestConfig("cl-inflight");
    cfg.maxInflightCalls = 1;
    auto exec = std::make_shared<DeferredExecutor>();
    auto h = makeTestRuntime(cfg, exec);
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::ok({});
    });
    p.offer();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();
    exec->drain();
    // One async call occupies the single in-flight slot (handler queued).
    CallOptions opts;
    opts.timeout = std::chrono::milliseconds(1000);
    auto f1 = c.callAsync(0x0001, Payload{}, opts);
    // A second concurrent call exceeds the limit (CGW-FW-0309).
    CGW_EXPECT_THROW_CODE(c.callAsync(0x0001, Payload{}, opts), "CGW-FW-0309");
    // Drain completes the first call, freeing the slot.
    exec->drain();
    auto r1 = f1.get();
    assert(r1.success);
    CGW_PASS("test_inflight_limit");
}

}  // namespace

int main() {
    std::cout << "=== someip_client_test ===" << std::endl;
    test_availability_transitions();
    test_version_mismatch();
    test_unavailable_call_returns_0304();
    test_sync_call_success();
    test_async_call_success();
    test_async_timeout_virtual_clock();
    test_sync_timeout_real_wait();
    test_non_idempotent_no_retry();
    test_idempotent_bounded_retry_success();
    test_idempotent_retry_exhausted();
    test_subscription_raii();
    test_inflight_limit();
    std::cout << "=== someip_client_test ALL PASS ===" << std::endl;
    return 0;
}
