// src/someip_impl.h - PRIVATE shared implementation state (CR-005 §5/§6/§7/§11).
//
// RuntimeImpl/ProviderImpl/ClientImpl are the PImpl types behind the public
// SomeIpRuntime/Provider/Client facades. They share this header so each .cpp
// can reference the others' complete type. Only ccu-framework-someip sources
// and HOST tests include this file (CR-005 §3).
#pragma once

#include "ccu/fw/someip/client.hpp"
#include "ccu/fw/someip/provider.hpp"
#include "ccu/fw/someip/runtime.hpp"
#include "ccu/fw/someip/types.hpp"
#include "someip_backend.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ccu::fw::someip {
namespace detail {

// RAII subscription control block (CR-005 §7). canceler is set by ClientImpl;
// cancelled guards idempotent cancel and prevents reconnect restore.
struct SubscriptionState {
    std::function<void()> canceler;
    bool cancelled{false};
};

// ============================================================
// Validation helpers (CR-005 §4/§10). Throw SomeIpException on violation.
// ============================================================
// 0x0000 and 0xFFFF are reserved for Service/Instance/Method/Eventgroup.
// Method IDs use 0x0001..0x7FFF; Event IDs use 0x8000..0xFFFE (SOME/IP event
// range). Invalid IDs/config yield CCU-FW-0302; illegal payload yields 0306.
void validateServiceKey(ServiceKey key);
void validateMethodId(MethodId id);
void validateEventId(EventId id);
void validateEventgroupId(EventgroupId id);
void validatePayload(PayloadView pv, std::size_t maxBytes);
void validateConfig(const SomeIpConfig& cfg);

// ============================================================
// RuntimeImpl
// ============================================================
class RuntimeImpl : public std::enable_shared_from_this<RuntimeImpl> {
public:
    explicit RuntimeImpl(SomeIpConfig config, std::shared_ptr<Executor> executor,
                         std::shared_ptr<DiagnosticsSink> diag);
    ~RuntimeImpl();

    // Lifecycle (CR-005 §5/§11).
    void start();
    void stop();
    bool running() const;
    RuntimeState state() const;

    // Provider/Client factories (CR-005 §5/§6/§7).
    std::shared_ptr<ProviderImpl> createProvider(ServiceKey key, InterfaceVersion ver);
    std::shared_ptr<ClientImpl> createClient(ServiceKey key, InterfaceVersion ver);

    // Accessors for Provider/Client impls.
    const SomeIpConfig& config() const { return m_config; }
    SomeIpBackend& backend() { return *m_backend; }
    Executor& executor() { return *m_executor; }
    void report(const DiagnosticsEvent& ev);

    // Called by Provider/Client to register/deregister their existence.
    void registerProvider();
    void unregisterProvider();
    void registerClient();
    void unregisterClient();

    // Identity tracking: duplicate application identity before start yields
    // CCU-FW-0302 (CR-005 §5). Each process has one Runtime; the registry is a
    // process-wide set guarded by a static mutex.
    static bool registerIdentity(const std::string& application);
    static void unregisterIdentity(const std::string& application);

private:
    friend TestRuntimeHandle makeTestRuntime(const SomeIpConfig&,
                                             std::shared_ptr<Executor>,
                                             std::shared_ptr<DiagnosticsSink>);
    // Test-only: construct with an explicit backend (CR-005 §14 fake injection).
    RuntimeImpl(SomeIpConfig config, std::shared_ptr<Executor> executor,
                std::shared_ptr<DiagnosticsSink> diag,
                std::shared_ptr<SomeIpBackend> backend);
    SomeIpConfig m_config;
    std::shared_ptr<Executor> m_executor;
    std::shared_ptr<DiagnosticsSink> m_diag;
    std::shared_ptr<SomeIpBackend> m_backend;

    mutable std::mutex m_mutex;
    RuntimeState m_state{RuntimeState::Created};
    std::size_t m_providerCount{0};
    std::size_t m_clientCount{0};
    std::string m_application;
    bool m_identityRegistered{false};

    void transition(RuntimeState expected, RuntimeState next, const char* op);
};

// ============================================================
// ProviderImpl (CR-005 §6)
// ============================================================
class ProviderImpl {
public:
    ProviderImpl(std::shared_ptr<RuntimeImpl> runtime, ServiceKey key,
                 InterfaceVersion ver);
    ~ProviderImpl();

    void registerMethod(MethodId id, MethodHandler handler);
    void offer();
    void stopOffer();
    void offerEvent(EventId event, EventgroupId group, EventOptions opts);
    void stopOfferEvent(EventId event);
    void notify(EventId event, PayloadView payload);
    bool offering() const;

    ServiceKey key() const { return m_key; }
    InterfaceVersion version() const { return m_version; }

private:
    std::shared_ptr<RuntimeImpl> m_runtime;
    ServiceKey m_key;
    InterfaceVersion m_version;
    mutable std::mutex m_mutex;
    std::unordered_map<MethodId, MethodHandler> m_methods;
    std::unordered_map<EventId, EventgroupId> m_offeredEvents;
    bool m_offering{false};
};

// ============================================================
// ClientImpl (CR-005 §7/§8)
// ============================================================
class ClientImpl : public std::enable_shared_from_this<ClientImpl> {
public:
    ClientImpl(std::shared_ptr<RuntimeImpl> runtime, ServiceKey key,
               InterfaceVersion ver);
    ~ClientImpl();

    void requestService();
    void releaseService();
    Availability availability() const;
    Subscription onAvailability(AvailabilityCallback callback);
    CallResult call(MethodId id, PayloadView payload, const CallOptions& options);
    Future<CallResult> callAsync(MethodId id, Payload payload, const CallOptions& options);
    Subscription subscribe(EventgroupId group, EventId event, EventCallback callback);

    ServiceKey key() const { return m_key; }
    InterfaceVersion version() const { return m_version; }

    // Build a Subscription around a control block (friend access via Client).
    static Subscription makeSubscription(std::shared_ptr<SubscriptionState> state);

private:
    struct InFlight {
        SessionId session{0};
        std::uint16_t clientId{0};
        std::string traceId;
        std::shared_ptr<std::promise<CallResult>> promise;
        std::atomic<bool> completed{false};
        std::uint64_t timerId{0};
        std::uint32_t attempt{0};
        RetryMode retry{RetryMode::None};
        std::uint32_t maxRetries{0};
        MethodId method{0};
        Payload payload;  // retained for retries (async path owns payload)
        CallOptions options;
        bool timerActive{false};
    };

    struct SubscriptionRecord {
        std::uint64_t backendSubId{0};
        std::shared_ptr<SubscriptionState> state;
        EventgroupId group{0};
        EventId event{0};
        EventCallback callback;
        bool active{false};
    };

    std::shared_ptr<RuntimeImpl> m_runtime;
    ServiceKey m_key;
    InterfaceVersion m_version;
    mutable std::mutex m_mutex;

    Availability m_availability{Availability::Unknown};
    std::map<std::uint64_t, AvailabilityCallback> m_availabilityCallbacks;
    std::uint64_t m_nextAvailHandle{1};
    std::atomic<std::uint16_t> m_nextSession{1};
    std::atomic<std::uint16_t> m_clientId{0};

    std::unordered_map<SessionId, std::shared_ptr<InFlight>> m_inFlight;
    std::unordered_map<std::uint64_t, SubscriptionRecord> m_subscriptions;
    std::uint64_t m_nextSubId{1};

    // Recovery backoff state (CR-005 §8).
    bool m_serviceRequested{false};
    std::uint64_t m_recoveryTimerId{0};
    int m_recoveryAttempt{0};

    void setAvailability(Availability next);
    void cancelAvailabilityCallback(std::uint64_t handleId);
    CallResult doSyncCall(MethodId id, PayloadView payload, const CallOptions& options);
    void sendCallRequest(std::shared_ptr<InFlight> entry);
    void onResponse(std::shared_ptr<InFlight> entry, CallResult result);
    void onTimeout(std::shared_ptr<InFlight> entry);
    void armRecovery();
    void onRecoveryTimer();
    void resubscribe();
    void cancelSubscription(std::uint64_t subId);
};

}  // namespace detail
}  // namespace ccu::fw::someip
