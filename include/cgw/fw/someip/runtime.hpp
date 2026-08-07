// cgw-framework-someip - Runtime / Application public API (CR-005 §5).
//
// SomeIpRuntime owns the per-process SOME/IP Application: a single application
// identity, the controlled backend and the Executor. State machine:
//   Created -> Starting -> Running -> Stopping -> Stopped
// start() must complete before offer/request; stop() rejects new operations,
// withdraws subscriptions/services, waits (bounded) for in-flight work, then
// stops the backend. stop() is idempotent (CR-005 §5/§11).
//
// routing mode is explicitly selected (external manager or a single embedded
// owner); the framework SHALL NOT auto-fallback and create multiple routing
// owners on the same host (CR-005 §5). The external routing manager is a
// platform stack process, NOT a CGW framework business daemon (CR-005 §1).
#pragma once

#include "cgw/fw/someip/client.hpp"
#include "cgw/fw/someip/provider.hpp"
#include "cgw/fw/someip/types.hpp"

#include <memory>

namespace cgw::fw::someip {

namespace detail { class RuntimeImpl; }

// Test-only factory handle (defined in tests/someip_test_util.h). Forward
// declared here so SomeIpRuntime can friend it without leaking the private
// backend type into the public API (CR-005 §14).
namespace detail {
struct TestRuntimeHandle;
TestRuntimeHandle makeTestRuntime(const SomeIpConfig& config,
                                  std::shared_ptr<Executor> executor,
                                  std::shared_ptr<DiagnosticsSink> diag);
}

enum class RuntimeState {
    Created,
    Starting,
    Running,
    Stopping,
    Stopped,
};

class SomeIpRuntime {
public:
    SomeIpRuntime() = default;

    // Construct a Runtime with the given config and optional Executor. If no
    // executor is given, a bounded worker pool is used (CR-005 §11). Duplicate
    // application identity before start yields CGW-FW-0302 (CR-005 §5). An
    // optional DiagnosticsSink MAY be injected; the default is no-op (§13).
    static SomeIpRuntime create(const SomeIpConfig& config,
                                std::shared_ptr<Executor> executor = {},
                                std::shared_ptr<DiagnosticsSink> diag = {});

    // Move the state machine Created -> Starting -> Running. Returns to Created
    // (and throws CGW-FW-0301) on init/start failure (CR-005 §5/§12).
    void start();

    // Move to Stopping -> Stopped. Rejects new operations, withdraws
    // subscriptions/services, waits (bounded by shutdownTimeout) for in-flight
    // work, then stops the backend. Idempotent (CR-005 §5/§11).
    void stop();

    // True while in Running state.
    bool running() const;

    RuntimeState state() const;

    // Create a Provider/Client for a service key and interface version. IDs and
    // versions are validated; conflicts return CGW-FW-0302 (CR-005 §5/§6/§7).
    // Resource limits (maxProviders/maxClients) are enforced (CGW-FW-0309).
    Provider createProvider(ServiceKey key, InterfaceVersion version);
    Client createClient(ServiceKey key, InterfaceVersion version);

    friend detail::TestRuntimeHandle detail::makeTestRuntime(const SomeIpConfig&,
                                                               std::shared_ptr<Executor>,
                                                               std::shared_ptr<DiagnosticsSink>);

private:
    std::shared_ptr<detail::RuntimeImpl> m_impl;
    explicit SomeIpRuntime(std::shared_ptr<detail::RuntimeImpl> impl)
        : m_impl(std::move(impl)) {}
};

}  // namespace cgw::fw::someip
