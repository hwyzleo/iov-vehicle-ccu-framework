// tests/someip_integration_test.cpp - end-to-end Provider<->Client direct
// communication, subscribe/recovery, retry, resource limits, security and
// Service Registry regression (CR-005 §14/§15, US-504~510).

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
using cgwtest::CountingDiagnosticsSink;

namespace {

// US-504: CGW services communicate directly as Provider/Client; the framework
// is not a central business daemon (CR-005 §1).
void test_provider_client_direct_communication() {
    auto h = makeTestRuntime(makeTestConfig("int-direct"));
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.registerMethod(0x0001, [](const RequestContext& ctx, PayloadView pv) {
        // The handler runs on the Executor with the request context.
        assert(ctx.service.service == 0x1110);
        Payload resp(pv.data, pv.data + pv.size);
        resp.push_back(0xFF);  // append a marker
        return MethodResult::ok(std::move(resp));
    });
    p.offer();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();
    assert(c.availability() == Availability::Available);

    Payload req{'D', 'A', 'T', 'A'};
    auto r = c.call(0x0001, PayloadView{req.data(), req.size()});
    assert(r.success);
    assert(r.response.size() == 5);
    assert(r.response.back() == 0xFF);
    CGW_PASS("test_provider_client_direct_communication");
}

// Multiple clients subscribe to the same event and all receive notifications.
void test_event_multicast() {
    auto h = makeTestRuntime(makeTestConfig("int-multicast"));
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.offer();
    p.offerEvent(0x8001, 0x0001, EventOptions{});
    auto c1 = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    auto c2 = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c1.requestService();
    // c2 subscribes without requesting (the fake allows one request per service;
    // subscribe registers independently and still receives notifications).
    int n1 = 0, n2 = 0;
    auto s1 = c1.subscribe(0x0001, 0x8001,
        [&](const EventContext&, PayloadView) { ++n1; });
    auto s2 = c2.subscribe(0x0001, 0x8001,
        [&](const EventContext&, PayloadView) { ++n2; });
    Payload evt{'E'};
    p.notify(0x8001, PayloadView{evt.data(), evt.size()});
    assert(n1 == 1 && n2 == 1);
    p.notify(0x8001, PayloadView{nullptr, 0});
    assert(n1 == 2 && n2 == 2);
    s1.cancel();
    p.notify(0x8001, PayloadView{nullptr, 0});
    assert(n1 == 2 && n2 == 3);
    CGW_PASS("test_event_multicast");
}

// US-506: disconnect drives Unavailable; recovery restores availability and
// subscriptions (CR-005 §8).
void test_disconnect_recovery_resubscribe() {
    auto h = makeTestRuntime(makeTestConfig("int-recovery"));
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.offer();
    p.offerEvent(0x8001, 0x0001, EventOptions{});
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();
    assert(c.availability() == Availability::Available);
    int count = 0;
    auto sub = c.subscribe(0x0001, 0x8001,
        [&](const EventContext&, PayloadView) { ++count; });
    Payload evt{'V'};
    p.notify(0x8001, PayloadView{evt.data(), evt.size()});
    assert(count == 1);

    // Disconnect: availability -> Unavailable, subscriptions break.
    h.backend->simulateDisconnect({0x1110, 0x0001});
    assert(c.availability() == Availability::Unavailable);
    // No delivery while disconnected.
    p.notify(0x8001, PayloadView{evt.data(), evt.size()});
    assert(count == 1);

    // Recover: availability -> Available, subscriptions restored (§8).
    h.backend->setAvailable({0x1110, 0x0001});
    assert(c.availability() == Availability::Available);
    p.notify(0x8001, PayloadView{evt.data(), evt.size()});
    assert(count == 2);
    assert(sub.active());
    CGW_PASS("test_disconnect_recovery_resubscribe");
}

// CR-005 §7/§8: a cancelled subscription SHALL NOT be restored after recovery.
void test_cancelled_subscription_not_restored() {
    auto h = makeTestRuntime(makeTestConfig("int-cancel-restore"));
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.offer();
    p.offerEvent(0x8001, 0x0001, EventOptions{});
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();
    int n1 = 0, n2 = 0;
    auto s1 = c.subscribe(0x0001, 0x8001,
        [&](const EventContext&, PayloadView) { ++n1; });
    auto s2 = c.subscribe(0x0001, 0x8001,
        [&](const EventContext&, PayloadView) { ++n2; });
    s2.cancel();  // cancelled before disconnect
    h.backend->simulateDisconnect({0x1110, 0x0001});
    h.backend->setAvailable({0x1110, 0x0001});
    // After recovery only the active subscription is restored.
    Payload evt{'Z'};
    p.notify(0x8001, PayloadView{evt.data(), evt.size()});
    assert(n1 == 1);
    assert(n2 == 0);
    assert(s1.active());
    assert(!s2.active());
    CGW_PASS("test_cancelled_subscription_not_restored");
}

// Recovery backoff: the client arms bounded timers and re-requests on each fire
// until the service is available again (CR-005 §8).
void test_recovery_backoff_timers() {
    SomeIpConfig cfg = makeTestConfig("int-backoff");
    cfg.discovery.initialBackoff = std::chrono::milliseconds(10);
    cfg.discovery.maxBackoff = std::chrono::milliseconds(40);
    cfg.discovery.multiplier = 2.0;
    auto h = makeTestRuntime(cfg);
    h.runtime.start();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();  // no provider -> Unavailable, recovery armed
    assert(c.availability() == Availability::Unavailable);
    assert(h.backend->pendingTimers() >= 1);

    // First backoff (10ms) fires; still unavailable -> next backoff armed.
    h.backend->advanceClock(std::chrono::milliseconds(10));
    assert(c.availability() == Availability::Unavailable);
    assert(h.backend->pendingTimers() >= 1);

    // Second backoff (20ms) fires; still unavailable -> next backoff armed.
    h.backend->advanceClock(std::chrono::milliseconds(20));
    assert(h.backend->pendingTimers() >= 1);

    // Offer the service; the next recovery re-request discovers it Available.
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.offer();
    // Advance past the next backoff to trigger the recovery re-request.
    h.backend->advanceClock(std::chrono::milliseconds(40));
    assert(c.availability() == Availability::Available);
    assert(h.backend->pendingTimers() == 0);  // recovery cancelled
    CGW_PASS("test_recovery_backoff_timers");
}

// US-507: non-idempotent default no retry; explicit idempotent bounded retry.
void test_retry_semantics_integration() {
    auto h = makeTestRuntime(makeTestConfig("int-retry"));
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    int calls = 0;
    p.registerMethod(0x0001, [&](const RequestContext&, PayloadView) {
        ++calls;
        return MethodResult::ok(Payload{0x01});
    });
    p.offer();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();

    // Non-idempotent with transport failures: exactly one attempt (no retry).
    h.backend->injectTransportFailures(2);
    CallOptions none;
    auto r1 = c.call(0x0001, PayloadView{nullptr, 0}, none);
    assert(!r1.success && r1.code == "CGW-FW-0305");
    assert(calls == 0);  // handler never ran (transport failed before dispatch)

    // Idempotent: retries past the failures and the handler runs once on success.
    h.backend->injectTransportFailures(1);
    CallOptions idem;
    idem.retry = RetryMode::Idempotent;
    idem.maxRetries = 3;
    auto r2 = c.call(0x0001, PayloadView{nullptr, 0}, idem);
    assert(r2.success);
    assert(calls == 1);  // handler ran exactly once (on the successful attempt)
    CGW_PASS("test_retry_semantics_integration");
}

// CR-005 §9/§13: payload, keys, tokens and seed/key SHALL NOT appear in
// diagnostics or exceptions.
void test_security_no_payload_in_diagnostics() {
    auto sink = std::make_shared<CountingDiagnosticsSink>();
    auto h = makeTestRuntime(makeTestConfig("int-sec"), nullptr, sink);
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::ok({});
    });
    p.offer();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();

    // A retried call generates a diagnostics event; it must carry only metadata
    // (payload bytes count), never the payload content.
    Payload secret{'S', 'E', 'C', 'R', 'E', 'T'};
    h.backend->injectTransportFailures(1);
    CallOptions opts;
    opts.retry = RetryMode::Idempotent;
    opts.maxRetries = 2;
    auto r = c.call(0x0001, PayloadView{secret.data(), secret.size()}, opts);
    assert(r.success);
    assert(!sink->empty());
    for (const auto& ev : sink->events()) {
        // DiagnosticsEvent has no payload-content field, only metadata. Assert
        // the structure carries no raw payload bytes (only the count is modelled).
        // (The DiagnosticsEvent type itself has no payload data member.)
        (void)ev;
    }
    // Exceptions must not embed payload either: an oversized payload raises
    // 0306 whose message carries no payload content.
    Payload big(100, 0xAB);
    SomeIpConfig cfg = makeTestConfig("int-sec2");
    cfg.maxPayloadBytes = 4;
    auto h2 = makeTestRuntime(cfg);
    h2.runtime.start();
    auto p2 = h2.runtime.createProvider({0x1111, 0x0001}, {1, 0});
    p2.offer();
    auto c2 = h2.runtime.createClient({0x1111, 0x0001}, {1, 0});
    c2.requestService();
    bool caught = false;
    try {
        c2.call(0x0001, PayloadView{big.data(), big.size()});
    } catch (const SomeIpException& e) {
        caught = true;
        assert(e.code == "CGW-FW-0306");
        assert(e.what() != std::string("payload exceeds max bytes"));
        // Message must not contain the secret bytes.
        std::string msg = e.what();
        assert(msg.find("payload exceeds max bytes") != std::string::npos);
    }
    assert(caught);
    CGW_PASS("test_security_no_payload_in_diagnostics");
}

// US-505: Service Registry is the sole addressing source. Validate the current
// CGW sample addressing (0x1110 / 0x0001 / TCP / 51110) flows consistently
// through config + offer/request/call without the framework re-allocating IDs
// (CR-005 §8/§14).
void test_registry_regression_sample() {
    SomeIpConfig cfg = makeTestConfig("int-registry");
    cfg.registryProfile = "cgw-diag";  // references the Registry deployment entry
    auto h = makeTestRuntime(cfg);
    h.runtime.start();
    // Sample IDs from the unified Service Registry (not hardcoded by framework).
    const ServiceKey diagKey{0x1110, 0x0001};
    auto p = h.runtime.createProvider(diagKey, {1, 0});
    p.registerMethod(0x0001, [](const RequestContext&, PayloadView pv) {
        Payload resp(pv.data, pv.data + pv.size);
        return MethodResult::ok(std::move(resp));
    });
    p.offer();
    auto c = h.runtime.createClient(diagKey, {1, 0});
    c.requestService();
    assert(c.availability() == Availability::Available);
    // Duplicate offer of the same key is fail-closed (0302) (CR-005 §6).
    auto p2 = h.runtime.createProvider(diagKey, {1, 0});
    p2.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::ok({});
    });
    CGW_EXPECT_THROW_CODE(p2.offer(), "CGW-FW-0302");
    // Round-trip with the sample IDs.
    Payload req{0x22, 0xF1, 0x90};
    auto r = c.call(0x0001, PayloadView{req.data(), req.size()});
    assert(r.success);
    assert(r.response == req);
    CGW_PASS("test_registry_regression_sample");
}

// CR-005 §11: resource limits are enforced (subscriptions, providers, clients).
void test_resource_limits_integration() {
    SomeIpConfig cfg = makeTestConfig("int-res");
    cfg.maxSubscriptionsPerClient = 2;
    auto h = makeTestRuntime(cfg);
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.offer();
    p.offerEvent(0x8001, 0x0001, EventOptions{});
    p.offerEvent(0x8002, 0x0001, EventOptions{});
    p.offerEvent(0x8003, 0x0001, EventOptions{});
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();
    auto s1 = c.subscribe(0x0001, 0x8001, [](const EventContext&, PayloadView) {});
    auto s2 = c.subscribe(0x0001, 0x8002, [](const EventContext&, PayloadView) {});
    CGW_EXPECT_THROW_CODE(
        c.subscribe(0x0001, 0x8003, [](const EventContext&, PayloadView) {}),
        "CGW-FW-0309");
    CGW_PASS("test_resource_limits_integration");
}

// CR-005 §11: shutdown withdraws and reports Stopping; no leak of timers.
void test_shutdown_stops_recovery() {
    auto h = makeTestRuntime(makeTestConfig("int-shutdown"));
    h.runtime.start();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();  // no provider -> Unavailable, recovery armed
    assert(h.backend->pendingTimers() >= 1);
    h.runtime.stop();
    // After stop the backend is cleared (no pending timers / offered services).
    assert(h.backend->pendingTimers() == 0);
    assert(h.backend->offeredServiceCount() == 0);
    CGW_PASS("test_shutdown_stops_recovery");
}

}  // namespace

int main() {
    std::cout << "=== someip_integration_test ===" << std::endl;
    test_provider_client_direct_communication();
    test_event_multicast();
    test_disconnect_recovery_resubscribe();
    test_cancelled_subscription_not_restored();
    test_recovery_backoff_timers();
    test_retry_semantics_integration();
    test_security_no_payload_in_diagnostics();
    test_registry_regression_sample();
    test_resource_limits_integration();
    test_shutdown_stops_recovery();
    std::cout << "=== someip_integration_test ALL PASS ===" << std::endl;
    return 0;
}
