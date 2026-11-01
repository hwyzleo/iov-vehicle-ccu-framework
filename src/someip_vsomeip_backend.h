// src/someip_vsomeip_backend.h - PRIVATE production SomeIpBackend (CR-005 §15).
//
// VsomeipBackend is the platform-approved production backend (migration step 2),
// implemented on top of COVESA vsomeip 3.x. It replaces FakeSomeIpBackend in
// builds configured with CCU_FW_SOMEIP_VSOMEIP=ON; the fake remains the backend
// for HOST tests, which inject it explicitly via detail::makeTestRuntime.
//
// Boundary (CR-005 §1/§2/§3/§12):
//   - PRIVATE: only ccu-framework-someip sources include this header. Public
//     headers never mention SomeIpBackend or vsomeip types, and no vsomeip type
//     appears in any declaration below.
//   - vsomeip enums/exceptions never leak: every outcome maps to a stable
//     CCU-FW-03xx code or "SOMEIP/0x<rc>" (CR-005 §12).
//   - Handlers/callbacks are posted to the injected Executor and never run on
//     a vsomeip I/O or dispatch thread, nor under an internal lock
//     (CR-005 §6/§11).
//   - Diagnostics/exceptions carry metadata only, never payload bytes
//     (CR-005 §9/§13).
//
// Routing: the process joins the host routing manager named by the vsomeip
// configuration (VSOMEIP_CONFIGURATION). This backend never elects itself
// routing manager and never synthesises Service/Instance/Method/Event IDs; all
// IDs arrive from the caller, sourced from the unified Service Registry.
#pragma once

#include "someip_backend.h"

#include <memory>

namespace ccu::fw::someip {
namespace detail {

// ============================================================
// VsomeipBackend - production backend over COVESA vsomeip 3.x
// ============================================================
class VsomeipBackend : public SomeIpBackend {
public:
    // `config.application` is the vsomeip application identity; it MUST be
    // unique per process and resolvable by the vsomeip configuration in use.
    // Construction touches no network state; start() performs init/registration.
    VsomeipBackend(std::shared_ptr<Executor> executor, SomeIpConfig config);
    ~VsomeipBackend() override;

    VsomeipBackend(const VsomeipBackend&) = delete;
    VsomeipBackend& operator=(const VsomeipBackend&) = delete;

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

    // Real-time timers. advanceClock() keeps the base-class no-op: the
    // production backend is driven by the system steady clock (CR-005 §14).
    std::uint64_t scheduleTimer(std::chrono::milliseconds delay, TimerCallback cb) override;
    void cancelTimer(std::uint64_t timerId) override;
    std::chrono::milliseconds now() const override;

    // --- Observability (exposes no vsomeip types) ---
    bool started() const;
    bool registered() const;
    std::size_t inFlight() const;
    std::size_t pendingTimers() const;

    // Defined in the .cpp; public so the translation unit's vsomeip callback
    // lambdas can name it without befriending every helper. The type itself is
    // incomplete outside that translation unit, so it leaks nothing.
    struct State;

private:
    std::unique_ptr<State> m_state;
};

}  // namespace detail
}  // namespace ccu::fw::someip
