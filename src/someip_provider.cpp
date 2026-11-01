// src/someip_provider.cpp - ProviderImpl (CR-005 §6/§11).

#include "someip_impl.h"

#include <utility>

namespace ccu::fw::someip {
namespace detail {

ProviderImpl::ProviderImpl(std::shared_ptr<RuntimeImpl> runtime, ServiceKey key,
                           InterfaceVersion ver)
    : m_runtime(std::move(runtime)), m_key(key), m_version(ver) {}

ProviderImpl::~ProviderImpl() {
    try {
        if (m_offering) stopOffer();
    } catch (...) {
        // Best-effort cleanup (CCU-FW-0310); never throw from destructor.
    }
    if (m_runtime) m_runtime->unregisterProvider();
}

void ProviderImpl::registerMethod(MethodId id, MethodHandler handler) {
    validateMethodId(id);
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_offering) {
        throw SomeIpException("CCU-FW-0302",
                              "registerMethod: must complete before offer()");
    }
    if (m_methods.count(id)) {
        throw SomeIpException("CCU-FW-0302", "registerMethod: duplicate method id");
    }
    m_methods.emplace(id, std::move(handler));
}

void ProviderImpl::offer() {
    if (!m_runtime->running()) {
        throw SomeIpException("CCU-FW-0301", "offer: runtime not running");
    }
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_offering) return;  // idempotent re-offer
        m_offering = true;
    }
    // Register with the backend and install a dispatcher that looks up the
    // registered handler under a short lock, then invokes it outside the lock
    // on the Executor (CR-005 §6/§11). At most one response is sent per
    // request (the dispatcher returns a single MethodResult; the backend sends
    // exactly one response).
    m_runtime->backend().offerService(m_key, m_version);
    auto self = this;
    m_runtime->backend().setMethodDispatcher(
        m_key, [self](const RequestContext& ctx, PayloadView payload) -> MethodResult {
            MethodHandler handler;
            {
                std::lock_guard<std::mutex> lk(self->m_mutex);
                auto it = self->m_methods.find(ctx.method);
                if (it == self->m_methods.end()) {
                    return MethodResult::error(0x05, "method not registered");
                }
                handler = it->second;
            }
            // Handler runs on the Executor thread, outside the registry lock
            // (CR-005 §6/§11). Exceptions propagate to the backend, which maps
            // them to CCU-FW-0307 / a SOME/IP error response (CR-005 §12).
            return handler(ctx, payload);
        });
}

void ProviderImpl::stopOffer() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_offering) return;  // idempotent
        m_offering = false;
    }
    // Withdraw events and the service from the backend. The backend clears
    // matching subscribers/availability. In-flight handlers are bounded by the
    // Executor; production waits for them up to shutdownTimeout (CR-005 §6/§11).
    m_runtime->backend().stopOfferService(m_key);
}

void ProviderImpl::offerEvent(EventId event, EventgroupId group, EventOptions opts) {
    validateEventId(event);
    validateEventgroupId(group);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_offering) {
            throw SomeIpException("CCU-FW-0303", "offerEvent: provider not offering");
        }
        if (m_offeredEvents.count(event)) {
            throw SomeIpException("CCU-FW-0302", "offerEvent: event already offered");
        }
        m_offeredEvents.emplace(event, group);
    }
    m_runtime->backend().offerEvent(m_key, event, group, std::move(opts));
}

void ProviderImpl::stopOfferEvent(EventId event) {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_offeredEvents.erase(event);
    }
    m_runtime->backend().stopOfferEvent(m_key, event);
}

void ProviderImpl::notify(EventId event, PayloadView payload) {
    validateEventId(event);
    validatePayload(payload, m_runtime->config().maxPayloadBytes);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_offering) {
            throw SomeIpException("CCU-FW-0308", "notify: provider not offering");
        }
        if (!m_offeredEvents.count(event)) {
            throw SomeIpException("CCU-FW-0308", "notify: event not offered");
        }
    }
    m_runtime->backend().notify(m_key, event, payload);
}

bool ProviderImpl::offering() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_offering;
}

}  // namespace detail

// ============================================================
// Provider facade (delegates to ProviderImpl)
// ============================================================
void Provider::registerMethod(MethodId id, MethodHandler handler) {
    m_impl->registerMethod(id, std::move(handler));
}
void Provider::offer() { m_impl->offer(); }
void Provider::stopOffer() { m_impl->stopOffer(); }
void Provider::offerEvent(EventId event, EventgroupId group, const EventOptions& options) {
    m_impl->offerEvent(event, group, options);
}
void Provider::stopOfferEvent(EventId event) { m_impl->stopOfferEvent(event); }
void Provider::notify(EventId event, PayloadView payload) {
    m_impl->notify(event, payload);
}
bool Provider::offering() const { return m_impl->offering(); }

}  // namespace ccu::fw::someip
