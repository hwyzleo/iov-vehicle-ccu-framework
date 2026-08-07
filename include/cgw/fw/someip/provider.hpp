// cgw-framework-someip - Provider public API (CR-005 §6).
//
// A Provider offers a Service+Instance with a declared InterfaceVersion,
// registers Method handlers and offers/notifies Events. At most one response is
// sent per request; handler exceptions map to a SOME/IP error response
// (CGW-FW-0307). registerMethod MUST complete before offer(). stopOffer stops
// new dispatch, waits (bounded) for in-flight handlers, then withdraws events
// and the service (CR-005 §6/§11).
#pragma once

#include "cgw/fw/someip/types.hpp"

#include <functional>
#include <memory>

namespace cgw::fw::someip {

class SomeIpRuntime;  // friend: constructs Provider instances

namespace detail { class ProviderImpl; }

// Method handlers run on the injected Executor, never under an internal lock
// (CR-005 §6/§11). A handler returning MethodResult::ok() sends a response with
// the response payload; error() sends a SOME/IP error response.
using MethodHandler = std::function<MethodResult(const RequestContext&, PayloadView)>;

class Provider {
public:
    Provider() = default;

    // Register a Method handler. MUST be called before offer(). Duplicate,
    // reserved or interface-conflicting IDs return CGW-FW-0302 (CR-005 §6).
    void registerMethod(MethodId id, MethodHandler handler);

    // Offer the service/instance on SOME/IP-SD. After offer, incoming requests
    // are dispatched to registered handlers on the Executor (CR-005 §6/§8).
    void offer();

    // Stop accepting new requests, wait (bounded) for in-flight handlers, then
    // withdraw events and the service. Idempotent (CR-005 §6/§11).
    void stopOffer();

    // Offer an Event in an Eventgroup. notify() delivers to subscribed clients.
    void offerEvent(EventId event, EventgroupId group, const EventOptions& options = {});

    // Withdraw an event offer; idempotent.
    void stopOfferEvent(EventId event);

    // Notify all subscribers of an event with the given payload. Fails with
    // CGW-FW-0308 if the event is not offered or notify dispatch fails
    // (CR-005 §6/§8). PayloadView{nullptr,0} is a valid empty notify.
    void notify(EventId event, PayloadView payload);

    // True if offer() has been called and stopOffer() has not completed.
    bool offering() const;

private:
    std::shared_ptr<detail::ProviderImpl> m_impl;
    friend class SomeIpRuntime;
    explicit Provider(std::shared_ptr<detail::ProviderImpl> impl)
        : m_impl(std::move(impl)) {}
};

}  // namespace cgw::fw::someip
