// tests/someip_provider_test.cpp - Provider offer/stop, Method dispatch, one
// response, handler exception, Event offer/notify and payload validation
// (CR-005 §6/§12/§14).

#include "someip_test_util.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace cgw::fw::someip;
using cgwtest::makeTestConfig;
using cgwtest::makeTestRuntime;

namespace {

// Helper: start a runtime and offer a provider with a single echo method.
struct EchoFixture {
    cgwtest::someip::detail::TestRuntimeHandle h;
    Provider provider;
    Client client;
    EchoFixture(const std::string& app)
        : h(makeTestRuntime(makeTestConfig(app)))
        , provider(h.runtime.createProvider({0x1110, 0x0001}, {1, 0}))
        , client(h.runtime.createClient({0x1110, 0x0001}, {1, 0})) {
        h.runtime.start();
    }
};

void test_offer_requires_running() {
    auto h = makeTestRuntime(makeTestConfig("pr-offer"));
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::ok({});
    });
    CGW_EXPECT_THROW_CODE(p.offer(), "CGW-FW-0301");
    h.runtime.start();
    p.offer();  // now ok
    assert(p.offering());
    CGW_PASS("test_offer_requires_running");
}

void test_register_before_offer() {
    auto h = makeTestRuntime(makeTestConfig("pr-reg"));
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.offer();
    // registerMethod after offer is rejected (CR-005 §6).
    CGW_EXPECT_THROW_CODE(
        p.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
            return MethodResult::ok({});
        }), "CGW-FW-0302");
    CGW_PASS("test_register_before_offer");
}

void test_duplicate_method() {
    auto h = makeTestRuntime(makeTestConfig("pr-dup"));
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::ok({});
    });
    CGW_EXPECT_THROW_CODE(
        p.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
            return MethodResult::ok({});
        }), "CGW-FW-0302");
    // Reserved/out-of-range method id.
    CGW_EXPECT_THROW_CODE(
        p.registerMethod(0x0000, [](const RequestContext&, PayloadView) {
            return MethodResult::ok({});
        }), "CGW-FW-0302");
    CGW_EXPECT_THROW_CODE(
        p.registerMethod(0x8000, [](const RequestContext&, PayloadView) {
            return MethodResult::ok({});
        }), "CGW-FW-0302");
    CGW_PASS("test_duplicate_method");
}

void test_method_dispatch_and_response() {
    EchoFixture f("pr-dispatch");
    std::string received;
    f.provider.registerMethod(0x0001, [](const RequestContext& ctx,
                                         PayloadView pv) {
        // RequestContext carries the service/instance/method/client id.
        assert(ctx.service.service == 0x1110);
        assert(ctx.service.instance == 0x0001);
        assert(ctx.method == 0x0001);
        std::string s(reinterpret_cast<const char*>(pv.data), pv.size);
        Payload resp(s.begin(), s.end());
        return MethodResult::ok(std::move(resp));
    });
    f.provider.offer();
    f.client.requestService();
    assert(f.client.availability() == Availability::Available);

    Payload req{'h', 'i'};
    auto r = f.client.call(0x0001, PayloadView{req.data(), req.size()});
    assert(r.success);
    assert(r.response.size() == 2);
    assert(r.response[0] == 'h' && r.response[1] == 'i');
    CGW_PASS("test_method_dispatch_and_response");
}

void test_unregistered_method_error() {
    EchoFixture f("pr-unreg");
    f.provider.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::ok({});
    });
    f.provider.offer();
    f.client.requestService();
    // Calling a method that was not registered yields a method-not-found error
    // (mapped SOME/IP error) (CR-005 §6).
    auto r = f.client.call(0x0002, PayloadView{nullptr, 0});
    assert(!r.success);
    assert(r.code.find("SOMEIP/") == 0);
    CGW_PASS("test_unregistered_method_error");
}

void test_handler_exception_maps_0307() {
    EchoFixture f("pr-exc");
    f.provider.registerMethod(0x0001, [](const RequestContext&, PayloadView) -> MethodResult {
        throw std::runtime_error("boom");
    });
    f.provider.offer();
    f.client.requestService();
    auto r = f.client.call(0x0001, PayloadView{nullptr, 0});
    assert(!r.success);
    assert(r.code == "CGW-FW-0307");
    CGW_PASS("test_handler_exception_maps_0307");
}

void test_method_error_response() {
    EchoFixture f("pr-err");
    f.provider.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::error(0x11, "denied");
    });
    f.provider.offer();
    f.client.requestService();
    auto r = f.client.call(0x0001, PayloadView{nullptr, 0});
    assert(!r.success);
    assert(r.code == "SOMEIP/17");  // 0x11 == 17
    CGW_PASS("test_method_error_response");
}

void test_event_offer_notify() {
    EchoFixture f("pr-event");
    f.provider.offer();
    f.client.requestService();
    f.provider.offerEvent(0x8001, 0x0001, EventOptions{});

    std::vector<Payload> received;
    auto sub = f.client.subscribe(0x0001, 0x8001,
        [&](const EventContext& ctx, PayloadView pv) {
            assert(ctx.event == 0x8001);
            assert(ctx.group == 0x0001);
            received.emplace_back(pv.data, pv.data + pv.size);
        });
    Payload evt{'A', 'B', 'C'};
    f.provider.notify(0x8001, PayloadView{evt.data(), evt.size()});
    assert(received.size() == 1);
    assert(received[0].size() == 3 && received[0][0] == 'A');
    // stopOfferEvent then notify fails.
    f.provider.stopOfferEvent(0x8001);
    CGW_EXPECT_THROW_CODE(
        f.provider.notify(0x8001, PayloadView{nullptr, 0}), "CGW-FW-0308");
    CGW_PASS("test_event_offer_notify");
}

void test_notify_not_offered() {
    EchoFixture f("pr-notify");
    f.provider.offer();
    f.client.requestService();
    // Event not offered -> 0308.
    CGW_EXPECT_THROW_CODE(
        f.provider.notify(0x8001, PayloadView{nullptr, 0}), "CGW-FW-0308");
    // Provider not offering -> 0308.
    auto p2 = f.h.runtime.createProvider({0x1111, 0x0001}, {1, 0});
    CGW_EXPECT_THROW_CODE(
        p2.notify(0x8001, PayloadView{nullptr, 0}), "CGW-FW-0308");
    CGW_PASS("test_notify_not_offered");
}

void test_payload_validation_on_notify() {
    SomeIpConfig cfg = makeTestConfig("pr-payload");
    cfg.maxPayloadBytes = 4;
    auto h = makeTestRuntime(cfg);
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.offer();
    p.offerEvent(0x8001, 0x0001, EventOptions{});
    Payload big{1, 2, 3, 4, 5};  // 5 bytes > 4 limit
    CGW_EXPECT_THROW_CODE(
        p.notify(0x8001, PayloadView{big.data(), big.size()}), "CGW-FW-0306");
    // Non-zero length null pointer -> 0306.
    CGW_EXPECT_THROW_CODE(
        p.notify(0x8001, PayloadView{nullptr, 1}), "CGW-FW-0306");
    // Empty payload is valid.
    p.notify(0x8001, PayloadView{nullptr, 0});
    CGW_PASS("test_payload_validation_on_notify");
}

void test_stop_offer_idempotent() {
    EchoFixture f("pr-stopoffer");
    f.provider.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::ok({});
    });
    f.provider.offer();
    f.provider.stopOffer();
    f.provider.stopOffer();  // idempotent
    assert(!f.provider.offering());
    // Re-offer is allowed after stopOffer.
    f.provider.offer();
    assert(f.provider.offering());
    CGW_PASS("test_stop_offer_idempotent");
}

void test_binary_safe_payload() {
    EchoFixture f("pr-binary");
    // Payload with internal NULs and all byte values (CR-005 §4/§8, US-508).
    Payload req(256, 0);
    for (int i = 0; i < 256; ++i) req[i] = static_cast<std::uint8_t>(i);
    f.provider.registerMethod(0x0001, [](const RequestContext&, PayloadView pv) {
        Payload resp(pv.data, pv.data + pv.size);  // echo
        return MethodResult::ok(std::move(resp));
    });
    f.provider.offer();
    f.client.requestService();
    auto r = f.client.call(0x0001, PayloadView{req.data(), req.size()});
    assert(r.success);
    assert(r.response == req);
    CGW_PASS("test_binary_safe_payload");
}

}  // namespace

int main() {
    std::cout << "=== someip_provider_test ===" << std::endl;
    test_offer_requires_running();
    test_register_before_offer();
    test_duplicate_method();
    test_method_dispatch_and_response();
    test_unregistered_method_error();
    test_handler_exception_maps_0307();
    test_method_error_response();
    test_event_offer_notify();
    test_notify_not_offered();
    test_payload_validation_on_notify();
    test_stop_offer_idempotent();
    test_binary_safe_payload();
    std::cout << "=== someip_provider_test ALL PASS ===" << std::endl;
    return 0;
}
