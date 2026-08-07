// src/someip_client.cpp - ClientImpl + Subscription (CR-005 §7/§8/§11/§14).

#include "someip_impl.h"

#include <atomic>
#include <cmath>
#include <utility>

namespace cgw::fw::someip {
namespace detail {

namespace {

// Process-wide SOME/IP client id allocator (CR-005 §7).
std::atomic<std::uint16_t> g_nextClientId{1};

// Bounded discovery backoff: initial -> *multiplier -> max (CR-005 §8).
// Jitter is applied deterministically (alternating sign by attempt) so virtual
// clock tests are reproducible; production uses a random source.
std::chrono::milliseconds computeBackoff(int attempt,
                                         const DiscoveryConfig& d) {
    double base = static_cast<double>(d.initialBackoff.count());
    for (int i = 0; i < attempt; ++i) base *= d.multiplier;
    double maxv = static_cast<double>(d.maxBackoff.count());
    if (base > maxv) base = maxv;
    if (d.jitterPercent > 0) {
        double jit = base * (d.jitterPercent / 100.0);
        // Deterministic alternating jitter for reproducible tests.
        base += (attempt % 2 == 0) ? jit : -jit;
        if (base < 1.0) base = 1.0;
    }
    return std::chrono::milliseconds(static_cast<long long>(std::llround(base)));
}

}  // namespace

// Subscription is in cgw::fw::someip (not detail); close detail temporarily so
// the member definitions resolve to the correct class.
}  // namespace detail

// ============================================================
// Subscription (defined here where SubscriptionState is complete)
// ============================================================
Subscription::Subscription() = default;

Subscription::~Subscription() {
    cancel();
}

Subscription::Subscription(Subscription&&) noexcept = default;
Subscription& Subscription::operator=(Subscription&&) noexcept = default;

Subscription::Subscription(std::shared_ptr<detail::SubscriptionState> state)
    : m_state(std::move(state)) {}

void Subscription::cancel() {
    std::shared_ptr<detail::SubscriptionState> state;
    state.swap(m_state);
    if (state && !state->cancelled) {
        state->cancelled = true;
        if (state->canceler) state->canceler();
    }
}

bool Subscription::active() const noexcept {
    return m_state && !m_state->cancelled;
}

namespace detail {

Subscription ClientImpl::makeSubscription(std::shared_ptr<SubscriptionState> state) {
    return Subscription(std::move(state));
}

// ============================================================
// ClientImpl
// ============================================================
ClientImpl::ClientImpl(std::shared_ptr<RuntimeImpl> runtime, ServiceKey key,
                       InterfaceVersion ver)
    : m_runtime(std::move(runtime))
    , m_key(key)
    , m_version(ver)
    , m_clientId(g_nextClientId++) {}

ClientImpl::~ClientImpl() {
    try {
        releaseService();
        // Unsubscribe any remaining active subscriptions (RAII safety).
        std::vector<std::uint64_t> backendSubIds;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            backendSubIds.reserve(m_subscriptions.size());
            for (auto& kv : m_subscriptions) {
                if (kv.second.active && kv.second.backendSubId != 0) {
                    backendSubIds.push_back(kv.second.backendSubId);
                    kv.second.active = false;
                }
            }
            m_subscriptions.clear();
        }
        for (auto id : backendSubIds) {
            try { m_runtime->backend().unsubscribe(id); } catch (...) {}
        }
    } catch (...) {
        // Best-effort cleanup (CGW-FW-0310); never throw from destructor.
    }
    if (m_runtime) m_runtime->unregisterClient();
}

void ClientImpl::requestService() {
    if (!m_runtime->running()) {
        throw SomeIpException("CGW-FW-0301", "requestService: runtime not running");
    }
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_serviceRequested) {
            throw SomeIpException("CGW-FW-0302", "requestService: already requested");
        }
        m_serviceRequested = true;
        m_recoveryAttempt = 0;
    }
    auto self = shared_from_this();
    m_runtime->backend().requestService(
        m_key, m_version,
        [self](Availability a) { self->setAvailability(a); });
}

void ClientImpl::releaseService() {
    bool wasRequested;
    std::uint64_t recoveryTimer = 0;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        wasRequested = m_serviceRequested;
        m_serviceRequested = false;
        recoveryTimer = m_recoveryTimerId;
        m_recoveryTimerId = 0;
    }
    if (recoveryTimer) {
        try { m_runtime->backend().cancelTimer(recoveryTimer); } catch (...) {}
    }
    if (wasRequested) {
        try { m_runtime->backend().releaseService(m_key); } catch (...) {}
    }
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_availability = Availability::Unknown;
    }
}

Availability ClientImpl::availability() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_availability;
}

Subscription ClientImpl::onAvailability(AvailabilityCallback callback) {
    Availability current;
    auto state = std::make_shared<SubscriptionState>();
    auto self = shared_from_this();
    std::uint64_t handleId;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        handleId = m_nextAvailHandle++;
        current = m_availability;
        state->canceler = [self, handleId]() {
            self->cancelAvailabilityCallback(handleId);
        };
        m_availabilityCallbacks[handleId] = callback;
    }
    // Deliver the current state immediately (CR-005 §8: ordered serial delivery).
    if (callback) {
        m_runtime->executor().post([callback, current]() { callback(current); });
    }
    return makeSubscription(state);
}

void ClientImpl::cancelAvailabilityCallback(std::uint64_t handleId) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_availabilityCallbacks.erase(handleId);
}

CallResult ClientImpl::call(MethodId id, PayloadView payload,
                            const CallOptions& options) {
    validateMethodId(id);
    if (!m_runtime->running()) {
        throw SomeIpException("CGW-FW-0301", "call: runtime not running");
    }
    validatePayload(payload, m_runtime->config().maxPayloadBytes);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_availability != Availability::Available) {
            return CallResult::fail("CGW-FW-0304",
                                    "call: service unavailable or version mismatch");
        }
    }
    return doSyncCall(id, payload, options);
}

Future<CallResult> ClientImpl::callAsync(MethodId id, Payload payload,
                                         const CallOptions& options) {
    validateMethodId(id);
    if (!m_runtime->running()) {
        throw SomeIpException("CGW-FW-0301", "callAsync: runtime not running");
    }
    validatePayload(PayloadView{payload.data(), payload.size()},
                    m_runtime->config().maxPayloadBytes);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_availability != Availability::Available) {
            std::promise<CallResult> p;
            p.set_value(CallResult::fail(
                "CGW-FW-0304", "callAsync: service unavailable or version mismatch"));
            return p.get_future();
        }
    }
    auto entry = std::make_shared<InFlight>();
    entry->method = id;
    entry->payload = std::move(payload);
    entry->retry = options.retry;
    entry->maxRetries = options.maxRetries;
    entry->options = options;
    entry->clientId = m_clientId;
    entry->promise = std::make_shared<std::promise<CallResult>>();
    auto fut = entry->promise->get_future();
    // Schedule the async timeout on the virtual clock (CR-005 §7/§14).
    auto self = shared_from_this();
    entry->timerId = m_runtime->backend().scheduleTimer(
        options.timeout, [self, entry]() { self->onTimeout(entry); });
    entry->timerActive = true;
    try {
        sendCallRequest(entry);  // checks in-flight limit atomically (0309)
    } catch (...) {
        try { m_runtime->backend().cancelTimer(entry->timerId); } catch (...) {}
        throw;
    }
    return fut;
}

CallResult ClientImpl::doSyncCall(MethodId id, PayloadView payload,
                                  const CallOptions& options) {
    auto entry = std::make_shared<InFlight>();
    entry->method = id;
    entry->payload.assign(payload.data, payload.data + payload.size);
    entry->retry = options.retry;
    entry->maxRetries = options.maxRetries;
    entry->options = options;
    entry->clientId = m_clientId;
    entry->promise = std::make_shared<std::promise<CallResult>>();
    auto fut = entry->promise->get_future();
    sendCallRequest(entry);  // checks in-flight limit atomically (0309)
    // Sync path: block with a real-time bounded wait (CR-005 §7). onResponse
    // completes the future; a late response after timeout is discarded.
    if (fut.wait_for(options.timeout) == std::future_status::ready) {
        return fut.get();
    }
    // Timeout: mark completed so any late/duplicate response is discarded
    // (CR-005 §7).
    if (!entry->completed.exchange(true)) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_inFlight.erase(entry->session);
    }
    return CallResult::fail("CGW-FW-0305", "call timeout");
}

void ClientImpl::sendCallRequest(std::shared_ptr<InFlight> entry) {
    SessionId oldSession = entry->session;
    entry->session = m_nextSession++;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (oldSession == 0) {
            // First send for this entry: enforce the in-flight limit (0309).
            if (m_inFlight.size() >= m_runtime->config().maxInflightCalls) {
                entry->session = 0;
                throw SomeIpException("CGW-FW-0309",
                                      "call: in-flight limit exhausted");
            }
        } else {
            m_inFlight.erase(oldSession);  // retry: clean up previous session
        }
        m_inFlight[entry->session] = entry;
    }
    PayloadView pv{entry->payload.data(), entry->payload.size()};
    auto self = shared_from_this();
    m_runtime->backend().sendRequest(
        m_key, entry->method, pv, entry->session, m_clientId, entry->traceId,
        entry->options,
        [self, entry](CallResult r) { self->onResponse(entry, std::move(r)); });
}

void ClientImpl::onResponse(std::shared_ptr<InFlight> entry, CallResult result) {
    // Late/duplicate response (after timeout/cancel) is discarded (CR-005 §7).
    if (entry->completed.load()) {
        return;
    }
    // Retry only on transport failure for explicit idempotent calls (US-507).
    // Bounded by maxRetries; same trace semantics preserved (CR-005 §7).
    if (!result.success && result.code == "CGW-FW-0305" &&
        entry->retry == RetryMode::Idempotent &&
        entry->attempt < entry->maxRetries) {
        entry->attempt++;
        DiagnosticsEvent ev;
        ev.application = m_runtime->config().application;
        ev.service = m_key;
        ev.method = entry->method;
        ev.session = entry->session;
        ev.retryAttempt = static_cast<int>(entry->attempt);
        ev.code = "CGW-FW-0305";
        m_runtime->report(ev);
        sendCallRequest(entry);  // resend with a new session, same trace
        return;
    }
    // Complete the call exactly once.
    if (entry->completed.exchange(true)) {
        return;  // raced with timeout/cancel
    }
    if (entry->timerActive) {
        try { m_runtime->backend().cancelTimer(entry->timerId); } catch (...) {}
        entry->timerActive = false;
    }
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_inFlight.erase(entry->session);
    }
    entry->promise->set_value(std::move(result));
}

void ClientImpl::onTimeout(std::shared_ptr<InFlight> entry) {
    if (entry->completed.exchange(true)) {
        return;  // already completed by a response
    }
    entry->timerActive = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_inFlight.erase(entry->session);
    }
    entry->promise->set_value(CallResult::fail("CGW-FW-0305", "call timeout"));
}

Subscription ClientImpl::subscribe(EventgroupId group, EventId event,
                                   EventCallback callback) {
    validateEventgroupId(group);
    validateEventId(event);
    if (!m_runtime->running()) {
        throw SomeIpException("CGW-FW-0301", "subscribe: runtime not running");
    }
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_subscriptions.size() >= m_runtime->config().maxSubscriptionsPerClient) {
            throw SomeIpException("CGW-FW-0309",
                                  "subscribe: subscription limit exhausted");
        }
    }
    auto state = std::make_shared<SubscriptionState>();
    auto self = shared_from_this();
    std::uint64_t subId;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        subId = m_nextSubId++;
    }
    state->canceler = [self, subId]() { self->cancelSubscription(subId); };
    std::uint64_t backendSubId =
        m_runtime->backend().subscribe(m_key, group, event, callback);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        SubscriptionRecord rec;
        rec.state = state;
        rec.group = group;
        rec.event = event;
        rec.callback = callback;
        rec.backendSubId = backendSubId;
        rec.active = true;
        m_subscriptions[subId] = std::move(rec);
    }
    return makeSubscription(state);
}

void ClientImpl::cancelSubscription(std::uint64_t subId) {
    std::uint64_t backendSubId = 0;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_subscriptions.find(subId);
        if (it == m_subscriptions.end()) return;
        it->second.state->cancelled = true;
        it->second.active = false;
        backendSubId = it->second.backendSubId;
        m_subscriptions.erase(it);
    }
    if (backendSubId) {
        try { m_runtime->backend().unsubscribe(backendSubId); } catch (...) {}
    }
}

void ClientImpl::setAvailability(Availability next) {
    std::vector<AvailabilityCallback> cbs;
    std::vector<std::uint64_t> brokenSubIds;
    bool doCancelRecovery = false;
    bool doArmRecovery = false;
    bool doResubscribe = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        bool changed = (m_availability != next);
        m_availability = next;
        if (changed) {
            for (auto& kv : m_availabilityCallbacks) cbs.push_back(kv.second);
        }
        if (next == Availability::Available) {
            if (m_recoveryTimerId != 0) doCancelRecovery = true;
            doResubscribe = true;
        } else if (next == Availability::Unavailable) {
            if (m_serviceRequested && m_recoveryTimerId == 0) doArmRecovery = true;
            // Break active subscriptions: unsubscribe from the backend so no
            // delivery while disconnected (re-established on recovery) (§8).
            for (auto& kv : m_subscriptions) {
                if (kv.second.active && kv.second.backendSubId != 0) {
                    brokenSubIds.push_back(kv.second.backendSubId);
                    kv.second.active = false;
                    kv.second.backendSubId = 0;
                }
            }
        } else {
            // VersionMismatch / Stopping / Unknown: cancel recovery.
            if (m_recoveryTimerId != 0) doCancelRecovery = true;
        }
    }
    // Dispatch user callbacks on the executor, outside the lock (CR-005 §8/§11).
    for (auto& cb : cbs) {
        Availability a = next;
        m_runtime->executor().post([cb, a]() { cb(a); });
    }
    // Unsubscribe broken subscriptions from the backend (outside the lock).
    for (auto id : brokenSubIds) {
        try { m_runtime->backend().unsubscribe(id); } catch (...) {}
    }
    if (doCancelRecovery) {
        std::uint64_t tid;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            tid = m_recoveryTimerId;
            m_recoveryTimerId = 0;
        }
        if (tid) { try { m_runtime->backend().cancelTimer(tid); } catch (...) {} }
    }
    if (doResubscribe) {
        resubscribe();
    }
    if (doArmRecovery) {
        armRecovery();
    }
}

void ClientImpl::armRecovery() {
    auto self = shared_from_this();
    std::uint64_t oldId = 0;
    std::chrono::milliseconds delay{0};
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        oldId = m_recoveryTimerId;
        delay = computeBackoff(m_recoveryAttempt, m_runtime->config().discovery);
        m_recoveryAttempt++;
    }
    if (oldId) { try { m_runtime->backend().cancelTimer(oldId); } catch (...) {} }
    std::uint64_t newId = m_runtime->backend().scheduleTimer(
        delay, [self]() { self->onRecoveryTimer(); });
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_recoveryTimerId = newId;
    }
}

void ClientImpl::onRecoveryTimer() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_recoveryTimerId = 0;  // timer fired
        if (!m_serviceRequested) return;
    }
    // Re-request via a fresh SD discovery (release + request). The backend
    // recomputes availability and notifies setAvailability, which either
    // resubscribes (Available) or arms the next backoff (Unavailable).
    auto self = shared_from_this();
    try { m_runtime->backend().releaseService(m_key); } catch (...) {}
    m_runtime->backend().requestService(
        m_key, m_version,
        [self](Availability a) { self->setAvailability(a); });
}

void ClientImpl::resubscribe() {
    // Re-establish subscriptions that broke on disconnect. Cancelled
    // subscriptions SHALL NOT be restored (CR-005 §7/§8).
    std::vector<std::pair<std::uint64_t, SubscriptionRecord*>> toResub;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        for (auto& kv : m_subscriptions) {
            if (kv.second.state->cancelled) continue;
            if (kv.second.active) continue;
            toResub.emplace_back(kv.first, &kv.second);
        }
    }
    auto self = shared_from_this();
    for (auto& p : toResub) {
        // Re-subscribe in the backend; update the record.
        std::uint64_t backendSubId;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            auto it = m_subscriptions.find(p.first);
            if (it == m_subscriptions.end()) continue;
            if (it->second.state->cancelled) continue;
            // Drop any stale backend id first.
            backendSubId = it->second.backendSubId;
            it->second.backendSubId = 0;
        }
        if (backendSubId) {
            try { m_runtime->backend().unsubscribe(backendSubId); } catch (...) {}
        }
        EventgroupId g;
        EventId e;
        EventCallback cb;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            auto it = m_subscriptions.find(p.first);
            if (it == m_subscriptions.end() || it->second.state->cancelled) continue;
            g = it->second.group;
            e = it->second.event;
            cb = it->second.callback;
        }
        std::uint64_t newId = m_runtime->backend().subscribe(m_key, g, e, cb);
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            auto it = m_subscriptions.find(p.first);
            if (it != m_subscriptions.end() && !it->second.state->cancelled) {
                it->second.backendSubId = newId;
                it->second.active = true;
            } else {
                // Cancelled concurrently: drop the just-created subscription.
                try { m_runtime->backend().unsubscribe(newId); } catch (...) {}
            }
        }
    }
}

}  // namespace detail

// ============================================================
// Client facade (delegates to ClientImpl)
// ============================================================
void Client::requestService() { m_impl->requestService(); }
void Client::releaseService() { m_impl->releaseService(); }
Availability Client::availability() const { return m_impl->availability(); }
Subscription Client::onAvailability(AvailabilityCallback callback) {
    return m_impl->onAvailability(std::move(callback));
}
CallResult Client::call(MethodId id, PayloadView payload, const CallOptions& options) {
    return m_impl->call(id, payload, options);
}
Future<CallResult> Client::callAsync(MethodId id, Payload payload, const CallOptions& options) {
    return m_impl->callAsync(id, std::move(payload), options);
}
Subscription Client::subscribe(EventgroupId group, EventId event, EventCallback callback) {
    return m_impl->subscribe(group, event, std::move(callback));
}

}  // namespace cgw::fw::someip
