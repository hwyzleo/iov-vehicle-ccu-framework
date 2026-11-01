// tests/someip_test_util.h - deterministic test helpers for ccu-framework-someip
// (CR-005 §14). InlineExecutor runs callbacks synchronously; DeferredExecutor
// queues them for explicit drain (timeout tests). makeTestRuntime builds a
// Runtime backed by a controllable FakeSomeIpBackend.
#pragma once

#include "ccu/fw/someip/runtime.hpp"
#include "ccu/fw/someip/types.hpp"
#include "someip_backend.h"
#include "someip_impl.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace ccutest {
namespace someip = ccu::fw::someip;
using someip::detail::FakeSomeIpBackend;
using someip::detail::RuntimeImpl;

// Runs every posted task synchronously on the calling thread. Deterministic and
// single-threaded; suitable for most unit tests.
class InlineExecutor : public someip::Executor {
public:
    void post(std::function<void()> task) override { task(); }
    void shutdown() override {}
};

// Queues tasks; drain() runs them. Used for timeout/cancel tests where the
// handler must NOT run until the test advances the virtual clock.
class DeferredExecutor : public someip::Executor {
public:
    void post(std::function<void()> task) override { tasks_.push_back(std::move(task)); }
    void shutdown() override { tasks_.clear(); }
    std::size_t drain() {
        std::vector<std::function<void()>> t;
        t.swap(tasks_);
        for (auto& f : t) f();
        return t.size();
    }
    std::size_t pending() const { return tasks_.size(); }
private:
    std::vector<std::function<void()>> tasks_;
};

// Captures all diagnostics events for security/observability assertions.
class CountingDiagnosticsSink : public someip::DiagnosticsSink {
public:
    void report(const someip::DiagnosticsEvent& ev) override { events_.push_back(ev); }
    const std::vector<someip::DiagnosticsEvent>& events() const { return events_; }
    bool empty() const { return events_.empty(); }
private:
    std::vector<someip::DiagnosticsEvent> events_;
};

// A minimal valid config for tests. jitterPercent=0 for deterministic backoff.
inline someip::SomeIpConfig makeTestConfig(const std::string& app = "ccu-test") {
    someip::SomeIpConfig cfg;
    cfg.application = app;
    cfg.routingMode = someip::RoutingMode::External;
    cfg.maxPayloadBytes = 1024 * 1024;
    cfg.maxInflightCalls = 64;
    cfg.callbackQueueSize = 256;
    cfg.shutdownTimeout = std::chrono::milliseconds(1000);
    cfg.callTimeout = std::chrono::milliseconds(500);
    cfg.discovery.enabled = true;
    cfg.discovery.initialBackoff = std::chrono::milliseconds(10);
    cfg.discovery.maxBackoff = std::chrono::milliseconds(100);
    cfg.discovery.multiplier = 2.0;
    cfg.discovery.jitterPercent = 0;
    cfg.maxProviders = 8;
    cfg.maxClients = 8;
    cfg.maxSubscriptionsPerClient = 16;
    cfg.maxRetryTimers = 64;
    return cfg;
}

// Build a Runtime backed by a controllable FakeSomeIpBackend. Defined in
// ccu::fw::someip::detail so it matches the friend declarations granting access
// to the private SomeIpRuntime/RuntimeImpl constructors (CR-005 §14).
}  // namespace ccutest

namespace ccu::fw::someip::detail {

struct TestRuntimeHandle {
    SomeIpRuntime runtime;
    FakeSomeIpBackend* backend{nullptr};
};

inline TestRuntimeHandle makeTestRuntime(const SomeIpConfig& config,
                                         std::shared_ptr<Executor> executor = {},
                                         std::shared_ptr<DiagnosticsSink> diag = {}) {
    if (!executor) executor = std::make_shared<ccutest::InlineExecutor>();
    auto backend = std::make_shared<FakeSomeIpBackend>(executor);
    // Use `new` (not std::make_shared) so the private RuntimeImpl constructor is
    // invoked directly within this friended function (make_shared would construct
    // it from std:: and lose friend access) (CR-005 §14).
    auto impl = std::shared_ptr<RuntimeImpl>(new RuntimeImpl(config, executor, diag, backend));
    return TestRuntimeHandle{SomeIpRuntime(std::move(impl)), backend.get()};
}

}  // namespace ccu::fw::someip::detail

namespace ccutest {
using ccu::fw::someip::detail::makeTestRuntime;

// Assert that `stmt` throws a SomeIpException with the given code. The parameter
// is named `expected_code` (not `code`) to avoid colliding with the
// SomeIpException::code member access inside the macro body.
#define CCU_EXPECT_THROW_CODE(stmt, expected_code)                           \
    do {                                                                      \
        bool caught = false;                                                  \
        try { stmt; }                                                         \
        catch (const ccu::fw::someip::SomeIpException& e) {                   \
            caught = (e.code == (expected_code));                             \
            if (!caught) {                                                    \
                std::cerr << "  expected " << (expected_code) << " got " << e.code \
                          << ": " << e.what() << std::endl;                   \
            }                                                                 \
        } catch (...) { caught = false; }                                     \
        assert(caught);                                                       \
    } while (0)

#define CCU_PASS(name) std::cout << "  [PASS] " << (name) << std::endl

}  // namespace ccutest
