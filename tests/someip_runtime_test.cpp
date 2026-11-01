// tests/someip_runtime_test.cpp - Runtime state machine, identity, resource
// limits and config validation (CR-005 §5/§10/§14).

#include "someip_test_util.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using namespace ccu::fw::someip;
using ccutest::makeTestConfig;
using ccutest::makeTestRuntime;

namespace {

void test_state_transitions() {
    auto h = makeTestRuntime(makeTestConfig("rt-state"));
    assert(h.runtime.state() == RuntimeState::Created);
    assert(!h.runtime.running());
    h.runtime.start();
    assert(h.runtime.state() == RuntimeState::Running);
    assert(h.runtime.running());
    h.runtime.stop();
    assert(h.runtime.state() == RuntimeState::Stopped);
    assert(!h.runtime.running());
    CCU_PASS("test_state_transitions");
}

void test_stop_idempotent() {
    auto h = makeTestRuntime(makeTestConfig("rt-stop"));
    h.runtime.start();
    h.runtime.stop();
    h.runtime.stop();  // idempotent (CR-005 §5)
    h.runtime.stop();
    assert(h.runtime.state() == RuntimeState::Stopped);
    // stop on a never-started runtime goes straight to Stopped.
    auto h2 = makeTestRuntime(makeTestConfig("rt-stop2"));
    h2.runtime.stop();
    assert(h2.runtime.state() == RuntimeState::Stopped);
    CCU_PASS("test_stop_idempotent");
}

void test_start_not_running_before_start() {
    auto h = makeTestRuntime(makeTestConfig("rt-before"));
    // createProvider/offer require running; createProvider itself is allowed but
    // offer() must reject (checked in provider tests). Here we verify running()
    // is false and createClient then requestService rejects.
    assert(!h.runtime.running());
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    CCU_EXPECT_THROW_CODE(c.requestService(), "CCU-FW-0301");
    CCU_PASS("test_start_not_running_before_start");
}

void test_duplicate_identity() {
    auto h = makeTestRuntime(makeTestConfig("rt-dup"));
    // A second runtime with the same application identity is rejected before
    // start (CCU-FW-0302) (CR-005 §5).
    CCU_EXPECT_THROW_CODE(makeTestRuntime(makeTestConfig("rt-dup")),
                          "CCU-FW-0302");
    // After the first is stopped/destroyed, the identity is released.
    h.runtime.start();
    h.runtime.stop();
    CCU_PASS("test_duplicate_identity");
}

void test_invalid_config() {
    SomeIpConfig cfg = makeTestConfig("rt-cfg");
    cfg.application = "";  // empty identity
    CCU_EXPECT_THROW_CODE(makeTestRuntime(cfg), "CCU-FW-0302");

    cfg = makeTestConfig("rt-cfg2");
    cfg.maxInflightCalls = 0;
    CCU_EXPECT_THROW_CODE(makeTestRuntime(cfg), "CCU-FW-0302");

    cfg = makeTestConfig("rt-cfg3");
    cfg.discovery.initialBackoff = std::chrono::milliseconds(1000);
    cfg.discovery.maxBackoff = std::chrono::milliseconds(100);  // max < initial
    CCU_EXPECT_THROW_CODE(makeTestRuntime(cfg), "CCU-FW-0302");

    cfg = makeTestConfig("rt-cfg4");
    cfg.discovery.multiplier = 0.5;
    CCU_EXPECT_THROW_CODE(makeTestRuntime(cfg), "CCU-FW-0302");

    cfg = makeTestConfig("rt-cfg5");
    cfg.callTimeout = std::chrono::milliseconds(0);
    CCU_EXPECT_THROW_CODE(makeTestRuntime(cfg), "CCU-FW-0302");
    CCU_PASS("test_invalid_config");
}

void test_invalid_service_key() {
    auto h = makeTestRuntime(makeTestConfig("rt-key"));
    h.runtime.start();
    // Reserved service id 0x0000 (CR-005 §4).
    CCU_EXPECT_THROW_CODE(h.runtime.createProvider({0x0000, 0x0001}, {1, 0}),
                          "CCU-FW-0302");
    // Reserved service id 0xFFFF.
    CCU_EXPECT_THROW_CODE(h.runtime.createProvider({0xFFFF, 0x0001}, {1, 0}),
                          "CCU-FW-0302");
    // Reserved instance 0xFFFF (wildcard).
    CCU_EXPECT_THROW_CODE(h.runtime.createClient({0x1110, 0xFFFF}, {1, 0}),
                          "CCU-FW-0302");
    CCU_PASS("test_invalid_service_key");
}

void test_resource_limits() {
    SomeIpConfig cfg = makeTestConfig("rt-res");
    cfg.maxProviders = 2;
    cfg.maxClients = 1;
    auto h = makeTestRuntime(cfg);
    h.runtime.start();
    auto p1 = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    auto p2 = h.runtime.createProvider({0x1111, 0x0001}, {1, 0});
    // Third provider exceeds the limit (CCU-FW-0309).
    CCU_EXPECT_THROW_CODE(h.runtime.createProvider({0x1112, 0x0001}, {1, 0}),
                          "CCU-FW-0309");
    auto c1 = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    CCU_EXPECT_THROW_CODE(h.runtime.createClient({0x1111, 0x0001}, {1, 0}),
                          "CCU-FW-0309");
    CCU_PASS("test_resource_limits");
}

void test_default_executor_used() {
    // No executor injected: the default bounded worker pool is used. A simple
    // Provider/Client round-trip must still work (CR-005 §11).
    auto h = makeTestRuntime(makeTestConfig("rt-default"), nullptr);
    h.runtime.start();
    auto p = h.runtime.createProvider({0x1110, 0x0001}, {1, 0});
    p.registerMethod(0x0001, [](const RequestContext&, PayloadView) {
        return MethodResult::ok(Payload{0x42});
    });
    p.offer();
    auto c = h.runtime.createClient({0x1110, 0x0001}, {1, 0});
    c.requestService();
    // The default thread pool delivers availability asynchronously; wait for it.
    for (int i = 0; i < 2000 && c.availability() != Availability::Available; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(c.availability() == Availability::Available);
    auto r = c.call(0x0001, PayloadView{nullptr, 0});
    assert(r.success);
    assert(r.response.size() == 1 && r.response[0] == 0x42);
    h.runtime.stop();
    CCU_PASS("test_default_executor_used");
}

}  // namespace

int main() {
    std::cout << "=== someip_runtime_test ===" << std::endl;
    test_state_transitions();
    test_stop_idempotent();
    test_start_not_running_before_start();
    test_duplicate_identity();
    test_invalid_config();
    test_invalid_service_key();
    test_resource_limits();
    test_default_executor_used();
    std::cout << "=== someip_runtime_test ALL PASS ===" << std::endl;
    return 0;
}
