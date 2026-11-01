// ccu-framework-someip - Client public API (CR-005 §7/§8).
//
// A Client requests a Service+Instance, observes Availability, performs
// synchronous/asynchronous Method calls and subscribes to Events. Responses are
// associated by session/request identity; late/duplicate responses SHALL NOT
// complete another request (CR-005 §7). Non-idempotent calls default to no
// retry; only explicit Idempotent with maxRetries>0 may retry on transport
// failure, bounded and trace-preserving (US-507, CR-005 §7). Subscriptions are
// RAII; a cancelled subscription SHALL NOT be restored after reconnect
// (CR-005 §7/§8). Callbacks run on the Executor outside internal locks.
#pragma once

#include "ccu/fw/someip/types.hpp"

#include <functional>
#include <memory>

namespace ccu::fw::someip {

class SomeIpRuntime;  // friend: constructs Client instances

namespace detail { class ClientImpl; }

class Client {
public:
    Client() = default;

    // Request the service via SOME/IP-SD. Availability transitions drive
    // onAvailability callbacks (CR-005 §8).
    void requestService();

    // Release the service request and stop discovery/recovery (CR-005 §7).
    void releaseService();

    // Current observed availability (CR-005 §8).
    Availability availability() const;

    // Register an availability callback. Same-state transitions are coalesced;
    // callbacks are delivered serially in occurrence order (CR-005 §8).
    Subscription onAvailability(AvailabilityCallback callback);

    // Synchronous call. Checks Runtime running and Availability first; returns
    // CCU-FW-0304 when unavailable/version-mismatched, CCU-FW-0305 on
    // timeout/cancel/transport failure (CR-005 §7/§12).
    CallResult call(MethodId id, PayloadView payload, const CallOptions& options = {});

    // Asynchronous call returning a Future. The payload is moved (CR-005 §7).
    Future<CallResult> callAsync(MethodId id, Payload payload, const CallOptions& options = {});

    // Subscribe to an Event in an Eventgroup. On disconnect the client retries
    // request/subscribe with bounded backoff; a cancelled Subscription SHALL
    // NOT be restored (CR-005 §7/§8).
    Subscription subscribe(EventgroupId group, EventId event, EventCallback callback);

private:
    std::shared_ptr<detail::ClientImpl> m_impl;
    friend class SomeIpRuntime;
    explicit Client(std::shared_ptr<detail::ClientImpl> impl)
        : m_impl(std::move(impl)) {}
};

}  // namespace ccu::fw::someip
