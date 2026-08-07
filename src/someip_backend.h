// src/someip_backend.h - PRIVATE SomeIpBackend adapter (CR-005 §1/§2/§14).
//
// The backend abstracts the controlled SOME/IP stack. It is PRIVATE: only the
// cgw-framework-someip sources and HOST tests include this header. Public
// headers never mention SomeIpBackend or its concrete type (CR-005 §2/§3).
//
// FakeSomeIpBackend is the deterministic, no-network backend for this CR
// (migration step 1). It keeps an in-process registry so a Provider's Method
// handlers are reached directly by a Client's calls, and supports fault
// injection plus a virtual clock (CR-005 §14). The platform-approved production
// backend is wired in a follow-up CR (step 2).
#pragma once

#include "cgw/fw/someip/types.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace cgw::fw::someip {
namespace detail {

// Delivered to a Provider's registered dispatcher on an incoming request.
// Returns a MethodResult; the backend sends at most one response (CR-005 §6).
using MethodDispatcher = std::function<MethodResult(const RequestContext&, PayloadView)>;

// Delivered to a Client when a response (or mapped error) arrives. Late/
// duplicate responses (after timeout/cancel) SHALL be discarded by the client
// (CR-005 §7).
using ResponseCallback = std::function<void(CallResult)>;

// One-shot timer callback (virtual-clock driven in the fake).
using TimerCallback = std::function<void()>;

// ============================================================
// SomeIpBackend - abstract controlled-stack adapter (PRIVATE)
// ============================================================
class SomeIpBackend {
public:
    virtual ~SomeIpBackend() = default;

    // Application lifecycle (CR-005 §5).
    virtual void start() = 0;
    virtual void stop() = 0;

    // Provider operations (CR-005 §6).
    virtual void offerService(ServiceKey key, InterfaceVersion ver) = 0;
    virtual void stopOfferService(ServiceKey key) = 0;
    virtual void setMethodDispatcher(ServiceKey key, MethodDispatcher dispatch) = 0;
    virtual void offerEvent(ServiceKey key, EventId event, EventgroupId group,
                            EventOptions opts) = 0;
    virtual void stopOfferEvent(ServiceKey key, EventId event) = 0;
    virtual void notify(ServiceKey key, EventId event, PayloadView payload) = 0;

    // Client operations (CR-005 §7/§8).
    virtual void requestService(ServiceKey key, InterfaceVersion ver,
                                AvailabilityCallback cb) = 0;
    virtual void releaseService(ServiceKey key) = 0;
    // Send a request to `target`. The backend routes to the matching Provider
    // handler and delivers the response via onResponse. `session`/`clientId`/
    // `traceId` populate the RequestContext delivered to the handler.
    virtual void sendRequest(ServiceKey target, MethodId method, PayloadView payload,
                             SessionId session, std::uint16_t clientId,
                             const std::string& traceId, const CallOptions& opts,
                             ResponseCallback onResponse) = 0;
    virtual std::uint64_t subscribe(ServiceKey key, EventgroupId group, EventId event,
                                    EventCallback cb) = 0;
    virtual void unsubscribe(std::uint64_t subId) = 0;

    // Timers / virtual clock (CR-005 §14). Production backends run on real time
    // and MAY treat advanceClock as a no-op; the fake drives timers from it.
    virtual std::uint64_t scheduleTimer(std::chrono::milliseconds delay, TimerCallback cb) = 0;
    virtual void cancelTimer(std::uint64_t timerId) = 0;
    virtual std::chrono::milliseconds now() const = 0;
    virtual void advanceClock(std::chrono::milliseconds ms) { (void)ms; }
};

// ============================================================
// FakeSomeIpBackend - deterministic, no-network backend (HOST tests)
// ============================================================
class FakeSomeIpBackend : public SomeIpBackend {
public:
    explicit FakeSomeIpBackend(std::shared_ptr<Executor> executor);
    ~FakeSomeIpBackend() override;

    FakeSomeIpBackend(const FakeSomeIpBackend&) = delete;
    FakeSomeIpBackend& operator=(const FakeSomeIpBackend&) = delete;

    // --- SomeIpBackend ---
    void start() override;
    void stop() override;
    void offerService(ServiceKey key, InterfaceVersion ver) override;
    void stopOfferService(ServiceKey key) override;
    void setMethodDispatcher(ServiceKey key, MethodDispatcher dispatch) override;
    void offerEvent(ServiceKey key, EventId event, EventgroupId group,
                    EventOptions opts) override;
    void stopOfferEvent(ServiceKey key, EventId event) override;
    void notify(ServiceKey key, EventId event, PayloadView payload) override;
    void requestService(ServiceKey key, InterfaceVersion ver,
                        AvailabilityCallback cb) override;
    void releaseService(ServiceKey key) override;
    void sendRequest(ServiceKey target, MethodId method, PayloadView payload,
                     SessionId session, std::uint16_t clientId,
                     const std::string& traceId, const CallOptions& opts,
                     ResponseCallback onResponse) override;
    std::uint64_t subscribe(ServiceKey key, EventgroupId group, EventId event,
                            EventCallback cb) override;
    void unsubscribe(std::uint64_t subId) override;
    std::uint64_t scheduleTimer(std::chrono::milliseconds delay, TimerCallback cb) override;
    void cancelTimer(std::uint64_t timerId) override;
    std::chrono::milliseconds now() const override;
    void advanceClock(std::chrono::milliseconds ms) override;

    // --- Fault injection (test only, CR-005 §14) ---
    // Force a requested service into Unavailable (calls return CGW-FW-0304).
    void setServiceUnavailable(ServiceKey key, bool unavailable);
    // Force a requested service into VersionMismatch.
    void setVersionMismatch(ServiceKey key, bool mismatched);
    // Fail the next `count` sendRequest calls with a transport failure
    // (CGW-FW-0305), enabling idempotent retry tests (CR-005 §7/§14).
    void injectTransportFailures(int count);
    // Simulate a disconnect: drive Availability to Unavailable and mark all
    // subscriptions for the service as broken (recovery backoff may re-arm).
    void simulateDisconnect(ServiceKey key);
    // Force a service back to Available (recovery test helper).
    void setAvailable(ServiceKey key);

    // --- Observability (test only) ---
    bool started() const;
    std::size_t pendingTimers() const;
    std::size_t offeredServiceCount() const;
    std::size_t subscriptionCount() const;
    std::size_t inFlight() const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};

}  // namespace detail
}  // namespace cgw::fw::someip
