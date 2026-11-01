// src/someip_backend.cpp - FakeSomeIpBackend implementation (CR-005 §1/§14).
//
// Deterministic, no-network SOME/IP backend. Maintains an in-process registry
// so a Provider's Method handlers are reached directly by a Client's calls.
// Supports fault injection and a virtual clock for HOST tests (CR-005 §14).
// The platform-approved production backend is a follow-up CR (step 2).

#include "someip_backend.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace ccu::fw::someip {
namespace detail {

namespace {

CallResult transportFailure() {
    return CallResult::fail("CCU-FW-0305", "transport failure");
}
CallResult unavailableResult() {
    return CallResult::fail("CCU-FW-0304", "service unavailable");
}

}  // namespace

struct FakeSomeIpBackend::State {
    std::shared_ptr<Executor> executor;
    mutable std::mutex mutex;
    bool started{false};

    struct OfferedEvent {
        EventgroupId group{0};
        EventOptions options;
        std::vector<std::uint64_t> subscriberIds;
    };
    struct OfferedService {
        InterfaceVersion version;
        MethodDispatcher dispatcher;
        std::unordered_map<EventId, OfferedEvent> events;
    };
    std::map<ServiceKey, OfferedService> offered;

    struct RequestedService {
        InterfaceVersion version;
        AvailabilityCallback callback;
        Availability current{Availability::Unknown};
    };
    std::map<ServiceKey, RequestedService> requested;

    struct Subscription {
        ServiceKey service;
        EventgroupId group{0};
        EventId event{0};
        EventCallback callback;
    };
    std::unordered_map<std::uint64_t, Subscription> subscriptions;
    std::uint64_t nextSubId{1};

    struct Timer {
        std::uint64_t id{0};
        std::chrono::milliseconds deadline{0};
        TimerCallback callback;
    };
    std::vector<Timer> timers;
    std::uint64_t nextTimerId{1};
    std::chrono::milliseconds clock{0};

    // Fault injection.
    std::set<ServiceKey> unavailable;
    std::set<ServiceKey> versionMismatch;
    int transportFailuresRemaining{0};
    std::size_t inFlight{0};

    explicit State(std::shared_ptr<Executor> exec) : executor(std::move(exec)) {}

    // Compute the availability for a requested service given current state.
    Availability computeAvailability(ServiceKey key, InterfaceVersion ver) {
        if (versionMismatch.count(key)) return Availability::VersionMismatch;
        auto it = offered.find(key);
        if (it == offered.end()) return Availability::Unavailable;
        if (it->second.version.major != ver.major) return Availability::VersionMismatch;
        if (unavailable.count(key)) return Availability::Unavailable;
        return Availability::Available;
    }

    // Notify the requested service callback of a new availability. The callback
    // is posted to the executor so it runs outside the backend lock (CR-005 §11).
    void notifyAvailability(ServiceKey key, Availability next) {
        AvailabilityCallback cb;
        {
            std::lock_guard<std::mutex> lk(mutex);
            auto it = requested.find(key);
            if (it == requested.end()) return;
            if (it->second.current == next) return;  // coalesce same-state (CR-005 §8)
            it->second.current = next;
            cb = it->second.callback;
        }
        if (cb && executor) executor->post([cb, next]() { cb(next); });
    }
};

FakeSomeIpBackend::FakeSomeIpBackend(std::shared_ptr<Executor> executor)
    : m_state(std::make_unique<State>(std::move(executor))) {}

FakeSomeIpBackend::~FakeSomeIpBackend() = default;

void FakeSomeIpBackend::start() {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->started = true;
}

void FakeSomeIpBackend::stop() {
    // Stop accepting new operations, then clear runtime state (CR-005 §11).
    std::vector<TimerCallback> drained;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        m_state->started = false;
        // Deliver Stopping to all requested services (CR-005 §8).
        for (auto& kv : m_state->requested) {
            if (kv.second.current != Availability::Stopping && kv.second.callback) {
                kv.second.current = Availability::Stopping;
                auto cb = kv.second.callback;
                drained.push_back([cb]() { cb(Availability::Stopping); });
            }
        }
        m_state->offered.clear();
        m_state->requested.clear();
        m_state->subscriptions.clear();
        m_state->timers.clear();
        m_state->inFlight = 0;
    }
    // Fire Stopping callbacks outside the lock.
    if (m_state->executor) {
        for (auto& cb : drained) m_state->executor->post(std::move(cb));
    }
}

void FakeSomeIpBackend::offerService(ServiceKey key, InterfaceVersion ver) {
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        if (m_state->offered.count(key)) {
            throw SomeIpException("CCU-FW-0302", "offerService: service already offered");
        }
        FakeSomeIpBackend::State::OfferedService svc;
        svc.version = ver;
        m_state->offered.emplace(key, std::move(svc));
    }
    // A waiting client may now become available (recompute + notify unlocked).
    Availability next;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        auto it = m_state->requested.find(key);
        if (it == m_state->requested.end()) return;
        next = m_state->computeAvailability(key, it->second.version);
    }
    m_state->notifyAvailability(key, next);
}

void FakeSomeIpBackend::stopOfferService(ServiceKey key) {
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        m_state->offered.erase(key);
    }
    Availability next;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        auto it = m_state->requested.find(key);
        if (it == m_state->requested.end()) return;
        next = m_state->computeAvailability(key, it->second.version);
    }
    m_state->notifyAvailability(key, next);
}

void FakeSomeIpBackend::setMethodDispatcher(ServiceKey key, MethodDispatcher dispatch) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    auto it = m_state->offered.find(key);
    if (it == m_state->offered.end()) {
        throw SomeIpException("CCU-FW-0302", "setMethodDispatcher: service not offered");
    }
    it->second.dispatcher = std::move(dispatch);
}

void FakeSomeIpBackend::offerEvent(ServiceKey key, EventId event, EventgroupId group,
                                   EventOptions opts) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    auto it = m_state->offered.find(key);
    if (it == m_state->offered.end()) {
        throw SomeIpException("CCU-FW-0303", "offerEvent: service not offered");
    }
    if (it->second.events.count(event)) {
        throw SomeIpException("CCU-FW-0302", "offerEvent: event already offered");
    }
    FakeSomeIpBackend::State::OfferedEvent ev;
    ev.group = group;
    ev.options = opts;
    it->second.events.emplace(event, std::move(ev));
}

void FakeSomeIpBackend::stopOfferEvent(ServiceKey key, EventId event) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    auto it = m_state->offered.find(key);
    if (it != m_state->offered.end()) {
        it->second.events.erase(event);
    }
}

void FakeSomeIpBackend::notify(ServiceKey key, EventId event, PayloadView payload) {
    // Copy subscriber callbacks under lock, then dispatch outside the lock on
    // the executor (CR-005 §6/§11).
    std::vector<EventCallback> targets;
    EventContext ctx;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        auto it = m_state->offered.find(key);
        if (it == m_state->offered.end() || !it->second.events.count(event)) {
            throw SomeIpException("CCU-FW-0308", "notify: event not offered");
        }
        ctx.service = key;
        ctx.event = event;
        ctx.group = it->second.events[event].group;
        // Gather callbacks for all subscriptions matching this service+event.
        for (std::uint64_t subId : it->second.events[event].subscriberIds) {
            auto sit = m_state->subscriptions.find(subId);
            if (sit != m_state->subscriptions.end() && sit->second.event == event) {
                targets.push_back(sit->second.callback);
            }
        }
    }
    if (m_state->executor) {
        for (auto& cb : targets) {
            EventContext c = ctx;
            EventCallback f = std::move(cb);
            m_state->executor->post([f, c, payload]() { f(c, payload); });
        }
    }
}

void FakeSomeIpBackend::requestService(ServiceKey key, InterfaceVersion ver,
                                       AvailabilityCallback cb) {
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        if (m_state->requested.count(key)) {
            throw SomeIpException("CCU-FW-0302", "requestService: already requested");
        }
        FakeSomeIpBackend::State::RequestedService rs;
        rs.version = ver;
        rs.callback = cb;
        rs.current = m_state->computeAvailability(key, ver);
        m_state->requested.emplace(key, std::move(rs));
    }
    // Deliver the initial availability on the executor (outside the lock).
    Availability initial;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        initial = m_state->requested[key].current;
    }
    if (cb && m_state->executor) {
        m_state->executor->post([cb, initial]() { cb(initial); });
    }
}

void FakeSomeIpBackend::releaseService(ServiceKey key) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->requested.erase(key);
}

void FakeSomeIpBackend::sendRequest(ServiceKey target, MethodId method, PayloadView payload,
                                    SessionId session, std::uint16_t clientId,
                                    const std::string& traceId, const CallOptions& /*opts*/,
                                    ResponseCallback onResponse) {
    MethodDispatcher dispatch;
    bool transportFail = false;
    bool unavail = false;
    RequestContext ctx;
    ctx.service = target;
    ctx.method = method;
    ctx.session = session;
    ctx.clientId = clientId;
    ctx.traceId = traceId;

    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        if (!m_state->started) {
            // Outside-lock delivery below.
            unavail = true;
        } else if (m_state->transportFailuresRemaining > 0) {
            --m_state->transportFailuresRemaining;
            transportFail = true;
        } else {
            auto it = m_state->offered.find(target);
            if (it == m_state->offered.end() || m_state->unavailable.count(target) ||
                !it->second.dispatcher) {
                unavail = true;
            } else {
                dispatch = it->second.dispatcher;
            }
        }
        if (transportFail || unavail || dispatch) {
            ++m_state->inFlight;
        }
    }

    if (transportFail) {
        {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            if (m_state->inFlight > 0) --m_state->inFlight;
        }
        onResponse(transportFailure());
        return;
    }
    if (unavail) {
        {
            std::lock_guard<std::mutex> lk(m_state->mutex);
            if (m_state->inFlight > 0) --m_state->inFlight;
        }
        onResponse(unavailableResult());
        return;
    }

    // Post the handler invocation on the executor: run dispatch, map the
    // MethodResult to a CallResult, then deliver onResponse. Handler exceptions
    // map to CCU-FW-0307 (CR-005 §6/§12). At most one response is sent.
    State* state = m_state.get();
    state->executor->post([state, dispatch, ctx, payload, onResponse]() {
        CallResult result;
        try {
            MethodResult mr = dispatch(ctx, payload);
            if (mr.success) {
                result = CallResult::ok(std::move(mr.response));
            } else {
                // Provider business error -> mapped SOME/IP error response.
                result = CallResult::fail(
                    "SOMEIP/" + std::to_string(mr.errorCode),
                    mr.errorMessage.empty() ? "method error" : mr.errorMessage);
            }
        } catch (const SomeIpException& e) {
            result = CallResult::fail(e.code, e.what());
        } catch (const std::exception& e) {
            result = CallResult::fail("CCU-FW-0307", e.what());
        } catch (...) {
            result = CallResult::fail("CCU-FW-0307", "handler exception");
        }
        {
            std::lock_guard<std::mutex> lk(state->mutex);
            if (state->inFlight > 0) --state->inFlight;
        }
        onResponse(result);
    });
}

std::uint64_t FakeSomeIpBackend::subscribe(ServiceKey key, EventgroupId group,
                                           EventId event, EventCallback cb) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    std::uint64_t subId = m_state->nextSubId++;
    FakeSomeIpBackend::State::Subscription sub;
    sub.service = key;
    sub.group = group;
    sub.event = event;
    sub.callback = std::move(cb);
    m_state->subscriptions.emplace(subId, std::move(sub));
    // Attach to the offered event's subscriber list if present.
    auto it = m_state->offered.find(key);
    if (it != m_state->offered.end()) {
        auto eit = it->second.events.find(event);
        if (eit != it->second.events.end()) {
            eit->second.subscriberIds.push_back(subId);
        }
    }
    return subId;
}

void FakeSomeIpBackend::unsubscribe(std::uint64_t subId) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    auto it = m_state->subscriptions.find(subId);
    if (it == m_state->subscriptions.end()) return;
    ServiceKey key = it->second.service;
    EventId event = it->second.event;
    m_state->subscriptions.erase(it);
    auto oit = m_state->offered.find(key);
    if (oit != m_state->offered.end()) {
        auto eit = oit->second.events.find(event);
        if (eit != oit->second.events.end()) {
            auto& ids = eit->second.subscriberIds;
            ids.erase(std::remove(ids.begin(), ids.end(), subId), ids.end());
        }
    }
}

std::uint64_t FakeSomeIpBackend::scheduleTimer(std::chrono::milliseconds delay,
                                               TimerCallback cb) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    std::uint64_t id = m_state->nextTimerId++;
    FakeSomeIpBackend::State::Timer t;
    t.id = id;
    t.deadline = m_state->clock + delay;
    t.callback = std::move(cb);
    m_state->timers.push_back(std::move(t));
    return id;
}

void FakeSomeIpBackend::cancelTimer(std::uint64_t timerId) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->timers.erase(
        std::remove_if(m_state->timers.begin(), m_state->timers.end(),
                       [timerId](const FakeSomeIpBackend::State::Timer& t) {
                           return t.id == timerId;
                       }),
        m_state->timers.end());
}

std::chrono::milliseconds FakeSomeIpBackend::now() const {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    return m_state->clock;
}

void FakeSomeIpBackend::advanceClock(std::chrono::milliseconds ms) {
    // Fire all due timers in deadline order. Timers may schedule new timers
    // during firing; those are not fired in this pass (bounded, no recursion
    // storm) (CR-005 §11).
    std::vector<FakeSomeIpBackend::State::Timer> toFire;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        m_state->clock += ms;
        for (auto it = m_state->timers.begin(); it != m_state->timers.end();) {
            if (it->deadline <= m_state->clock) {
                toFire.push_back(std::move(*it));
                it = m_state->timers.erase(it);
            } else {
                ++it;
            }
        }
    }
    std::sort(toFire.begin(), toFire.end(),
              [](const FakeSomeIpBackend::State::Timer& a,
                 const FakeSomeIpBackend::State::Timer& b) {
                  return a.deadline < b.deadline;
              });
    for (auto& t : toFire) {
        if (t.callback) t.callback();
    }
}

// --- Fault injection ---

void FakeSomeIpBackend::setServiceUnavailable(ServiceKey key, bool unavailable) {
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        if (unavailable) m_state->unavailable.insert(key);
        else m_state->unavailable.erase(key);
    }
    Availability next;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        auto it = m_state->requested.find(key);
        if (it == m_state->requested.end()) return;
        next = m_state->computeAvailability(key, it->second.version);
    }
    m_state->notifyAvailability(key, next);
}

void FakeSomeIpBackend::setVersionMismatch(ServiceKey key, bool mismatched) {
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        if (mismatched) m_state->versionMismatch.insert(key);
        else m_state->versionMismatch.erase(key);
    }
    Availability next;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        auto it = m_state->requested.find(key);
        if (it == m_state->requested.end()) return;
        next = m_state->computeAvailability(key, it->second.version);
    }
    m_state->notifyAvailability(key, next);
}

void FakeSomeIpBackend::injectTransportFailures(int count) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    m_state->transportFailuresRemaining = count;
}

void FakeSomeIpBackend::simulateDisconnect(ServiceKey key) {
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        m_state->unavailable.insert(key);
    }
    m_state->notifyAvailability(key, Availability::Unavailable);
}

void FakeSomeIpBackend::setAvailable(ServiceKey key) {
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        m_state->unavailable.erase(key);
        m_state->versionMismatch.erase(key);
    }
    Availability next;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        auto it = m_state->requested.find(key);
        if (it == m_state->requested.end()) return;
        next = m_state->computeAvailability(key, it->second.version);
    }
    m_state->notifyAvailability(key, next);
}

// --- Observability ---

bool FakeSomeIpBackend::started() const {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    return m_state->started;
}

std::size_t FakeSomeIpBackend::pendingTimers() const {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    return m_state->timers.size();
}

std::size_t FakeSomeIpBackend::offeredServiceCount() const {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    return m_state->offered.size();
}

std::size_t FakeSomeIpBackend::subscriptionCount() const {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    return m_state->subscriptions.size();
}

std::size_t FakeSomeIpBackend::inFlight() const {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    return m_state->inFlight;
}

}  // namespace detail
}  // namespace ccu::fw::someip
