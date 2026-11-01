// ccu-framework-someip - CCU common SOME/IP capability.
// CCU-FW-DSN-CR-005: unified controlled backend adaptation, Runtime/Application,
// Provider/Client, SOME/IP-SD, Method/Event, config injection, thread resources,
// CCU-FW-03xx errors and migration tests.
//
// Boundary (CR-005 §1/§9/§13):
//   - Independent in-process C++ static library; no daemon, no Unix socket IPC
//     wire, no central business broker. CCU services link it on demand and act
//     directly as SOME/IP Provider/Client.
//   - config/log are injected by the host; someip does NOT read YAML and does
//     NOT depend on config/log/store/hash. DiagnosticsSink is optional and
//     defaults to no-op.
//   - framework only carries raw binary payload; the service or generated code
//     owns IDL codec. Full payload, keys, tokens, certs, SecurityAccess
//     seed/key SHALL NOT appear in exceptions or diagnostics.
//   - Service/Instance/Method/Event/Eventgroup IDs, versions, transport and
//     ports come from the unified Service Registry; framework only consumes and
//     validates, it is never a second allocation source.
//
// Public header exposes only C++17 standard types; the SomeIpBackend adapter is
// PRIVATE (src/someip_backend.{h,cpp}). The deterministic FakeSomeIpBackend is
// the backend for this CR (migration step 1); the platform-approved production
// backend is wired in a follow-up CR (step 2).
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ccu::fw::someip {

// ============================================================
// Identifiers (CR-005 §4)
// ============================================================
// All IDs are validated for protocol range and reserved values before
// registration/request (CR-005 §4). 0x0000 and 0xFFFF are reserved and rejected
// for Service/Instance/Method/Eventgroup. Event IDs use the SOME/IP event range
// (>= 0x8000); method IDs use 0x0001..0x7FFF.
using ServiceId = std::uint16_t;
using InstanceId = std::uint16_t;
using MethodId = std::uint16_t;
using EventId = std::uint16_t;
using EventgroupId = std::uint16_t;
using SessionId = std::uint16_t;

struct ServiceKey {
    ServiceId service{0};
    InstanceId instance{0};
    bool operator==(const ServiceKey& o) const {
        return service == o.service && instance == o.instance;
    }
    bool operator!=(const ServiceKey& o) const { return !(*this == o); }
    bool operator<(const ServiceKey& o) const {
        if (service != o.service) return service < o.service;
        return instance < o.instance;
    }
};

struct InterfaceVersion {
    std::uint8_t major{0};
    std::uint32_t minor{0};
};

// Binary-safe payload view; does not rely on a C-string terminator.
// PayloadView{nullptr, 0} is a valid empty payload; size > 0 && data == nullptr
// is illegal and yields CCU-FW-0306 (CR-005 §4/§8).
struct PayloadView {
    const std::uint8_t* data{nullptr};
    std::size_t size{0};
};

using Payload = std::vector<std::uint8_t>;

// ============================================================
// Availability / retry / call options (CR-005 §4/§7/§8)
// ============================================================
enum class Availability {
    Unknown,
    Unavailable,
    Available,
    VersionMismatch,
    Stopping,
};

enum class RetryMode {
    None,         // default: non-idempotent, no automatic retry (US-507)
    Idempotent,   // explicit: bounded retry only on transport failure
};

struct CallOptions {
    std::chrono::milliseconds timeout{3000};
    RetryMode retry{RetryMode::None};
    std::uint32_t maxRetries{0};
};

struct EventOptions {
    bool reliable{false};        // TCP vs UDP delivery hint
    std::size_t maxSubscribers{64};
};

// ============================================================
// Contexts and results (CR-005 §6/§7)
// ============================================================
// RequestContext is delivered to a Provider MethodHandler; EventContext to a
// Client EventCallback. They carry only metadata, never full payload secrets.
struct RequestContext {
    ServiceKey service{};
    MethodId method{0};
    SessionId session{0};
    std::uint16_t clientId{0};
    std::string traceId;
    std::string requestId;
};

struct EventContext {
    ServiceKey service{};
    EventId event{0};
    EventgroupId group{0};
    std::string traceId;
};

// Provider returns a MethodResult per request; at most one response is sent
// (CR-005 §6). success=false yields a SOME/IP error response (errorCode).
struct MethodResult {
    bool success{true};
    Payload response{};
    std::uint32_t errorCode{0};
    std::string errorMessage;
    static MethodResult ok(Payload resp) {
        MethodResult r;
        r.success = true;
        r.response = std::move(resp);
        return r;
    }
    static MethodResult error(std::uint32_t code, std::string msg) {
        MethodResult r;
        r.success = false;
        r.errorCode = code;
        r.errorMessage = std::move(msg);
        return r;
    }
};

// Client receives a CallResult; code is a stable CCU-FW-03xx or mapped
// SOME/IP error (CR-005 §12). Backend enums/exceptions never leak (CR-005 §12).
struct CallResult {
    bool success{false};
    Payload response{};
    std::string code;       // "CCU-FW-0304" / "CCU-FW-0305" / "SOMEIP/0x05" ...
    std::string message;
    static CallResult ok(Payload resp) {
        CallResult r;
        r.success = true;
        r.response = std::move(resp);
        return r;
    }
    static CallResult fail(std::string code_, std::string msg) {
        CallResult r;
        r.success = false;
        r.code = std::move(code_);
        r.message = std::move(msg);
        return r;
    }
};

// std::future convenience alias for async calls (CR-005 §7).
template <class T>
using Future = std::future<T>;

// Callback type aliases used by Client and the private backend (CR-005 §7/§8).
using AvailabilityCallback = std::function<void(Availability)>;
using EventCallback = std::function<void(const EventContext&, PayloadView)>;

// ============================================================
// Executor (CR-005 §11)
// ============================================================
// Callbacks/handlers run on an Executor, never on the I/O/SD thread and never
// under an internal lock. The default is a bounded worker pool; services MAY
// inject a conforming executor (e.g. a deterministic inline executor in tests).
class Executor {
public:
    virtual ~Executor() = default;
    // Post a task for asynchronous execution. Implementations SHOULD be bounded
    // and reject/defer overflow rather than grow unbounded (CR-005 §11).
    virtual void post(std::function<void()> task) = 0;
    // Called during shutdown to drain/reject queued work.
    virtual void shutdown() {}
};

// ============================================================
// DiagnosticsSink (CR-005 §13)
// ============================================================
// Optional host-injected sink. The default is no-op; someip does NOT link log.
// Fields carry only metadata; full payload, keys, tokens, certs and seed/key
// SHALL NOT be recorded.
struct DiagnosticsEvent {
    std::string application;
    std::string peer;
    ServiceKey service{};
    MethodId method{0};
    EventId event{0};
    EventgroupId group{0};
    std::string messageType;
    SessionId session{0};
    std::string requestId;
    std::size_t payloadBytes{0};
    std::chrono::milliseconds duration{0};
    Availability availability{Availability::Unknown};
    int retryAttempt{0};
    std::string code;  // CCU-FW-03xx
};

class DiagnosticsSink {
public:
    virtual ~DiagnosticsSink() = default;
    virtual void report(const DiagnosticsEvent& event) { (void)event; }
};

// ============================================================
// Configuration (CR-005 §10)
// ============================================================
// The host constructs SomeIpConfig from a ConfigSnapshot (common.someip.* and
// <svc>.someip.*) and injects it; someip does NOT read YAML (CR-005 §10).
enum class RoutingMode {
    External,   // an external routing manager (platform stack process) owns routing
    Embedded,   // this process is the single embedded routing owner
};

struct DiscoveryConfig {
    bool enabled{true};
    std::chrono::milliseconds initialBackoff{100};
    std::chrono::milliseconds maxBackoff{5000};
    double multiplier{2.0};
    int jitterPercent{20};  // 0..100
};

struct SomeIpConfig {
    RoutingMode routingMode{RoutingMode::External};
    std::size_t maxPayloadBytes{4 * 1024 * 1024};
    std::size_t maxInflightCalls{1024};
    std::size_t callbackQueueSize{2048};
    std::chrono::milliseconds shutdownTimeout{5000};
    std::chrono::milliseconds callTimeout{3000};
    DiscoveryConfig discovery;
    std::string application;      // unique per-process application/client identity
    std::string registryProfile;  // Service Registry deployment entry reference
    // Resource ceilings (CR-005 §11); overflow yields CCU-FW-0309.
    std::size_t maxProviders{64};
    std::size_t maxClients{64};
    std::size_t maxSubscriptionsPerClient{256};
    std::size_t maxRetryTimers{512};
};

// ============================================================
// SomeIpException (CCU-FW-0301~0310) (CR-005 §12)
// ============================================================
// code: CCU-FW-0301 Runtime/Application init/start failed
//       CCU-FW-0302 config/Registry/ID/version/identity conflict (fail-closed)
//       CCU-FW-0303 offer/request/discovery failed
//       CCU-FW-0304 unavailable/version mismatch
//       CCU-FW-0305 call timeout/cancel/transport failure
//       CCU-FW-0306 message/payload/codec boundary illegal
//       CCU-FW-0307 handler/response/business adapter exception
//       CCU-FW-0308 event offer/notify/subscribe/recovery failed
//       CCU-FW-0309 resource limit exhausted
//       CCU-FW-0310 shutdown/executor/backend internal failure
// Diagnostics/exceptions SHALL NOT carry full payload, tokens, certs or
// seed/key material (CR-005 §9/§13).
struct SomeIpException : std::runtime_error {
    std::string code;

    SomeIpException(std::string code_, const std::string& message)
        : std::runtime_error(message), code(std::move(code_)) {}
};

// ============================================================
// Subscription (CR-005 §7)
// ============================================================
// RAII handle returned by Client::onAvailability and Client::subscribe.
// cancel() is idempotent; destruction cancels if still active. A cancelled
// subscription SHALL NOT be restored after a reconnect (CR-005 §7/§8).
namespace detail { struct SubscriptionState; }

class Client;  // forward-declare; Client constructs Subscriptions (friend)
namespace detail { class ClientImpl; }  // ClientImpl constructs Subscriptions (friend)

class Subscription {
public:
    Subscription();
    ~Subscription();
    Subscription(Subscription&&) noexcept;
    Subscription& operator=(Subscription&&) noexcept;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    void cancel();
    bool active() const noexcept;

private:
    std::shared_ptr<detail::SubscriptionState> m_state;
    friend class Client;
    friend class detail::ClientImpl;
    explicit Subscription(std::shared_ptr<detail::SubscriptionState> state);
};

}  // namespace ccu::fw::someip
