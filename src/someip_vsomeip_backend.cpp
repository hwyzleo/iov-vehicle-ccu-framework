// src/someip_vsomeip_backend.cpp - production SomeIpBackend over vsomeip 3.x
// (CR-005 §15, migration step 2).
//
// Threading model
// ---------------
//   * vsomeip owns its I/O and dispatch threads. Every callback received from
//     vsomeip is handed to the injected Executor before any user handler runs,
//     so no handler executes on a vsomeip thread or under an internal lock
//     (CR-005 §6/§11).
//   * application::start() blocks for the application's lifetime, so it runs on
//     a dedicated thread owned by this backend.
//   * One timer thread services scheduleTimer/cancelTimer on the steady clock.
//
// Request/response correlation
// ----------------------------
//   vsomeip assigns the wire session inside application::send() for MT_REQUEST,
//   so the session is only knowable after send() returns. The pending map is
//   guarded by its own mutex (m_pending), held across send() + insert; vsomeip
//   never delivers an incoming message synchronously on the caller's thread, so
//   this cannot self-deadlock, and it closes the window in which a fast
//   response would arrive before its entry exists.
//
// Error mapping (CR-005 §12) - no vsomeip enum or exception escapes:
//   not registered / send path broken  -> CCU-FW-0305
//   offer/request/discovery failed     -> CCU-FW-0303
//   event offer/notify/subscribe       -> CCU-FW-0308
//   illegal or oversized payload view  -> CCU-FW-0306
//   dispatcher threw                   -> SOME/IP error response to the peer
//   non-E_OK return code               -> "SOMEIP/0x<rc>"
//   init/registration failure          -> CCU-FW-0301
//   resource ceiling exhausted         -> CCU-FW-0309
#include "someip_vsomeip_backend.h"

#include <vsomeip/vsomeip.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ccu::fw::someip {
namespace detail {

namespace {

constexpr std::chrono::milliseconds kRegistrationWait{5000};

std::string someipReturnCode(::vsomeip::return_code_e rc) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "SOMEIP/0x%02X",
                  static_cast<unsigned>(static_cast<std::uint8_t>(rc)));
    return std::string(buf);
}

// Map a framework MethodResult error code onto a SOME/IP return code. The
// framework uses small integers mirroring the SOME/IP return-code space;
// anything outside it collapses to E_NOT_OK rather than truncating silently.
::vsomeip::return_code_e toReturnCode(std::uint32_t code) {
    if (code == 0 || code > 0x3F) return ::vsomeip::return_code_e::E_NOT_OK;
    return static_cast<::vsomeip::return_code_e>(static_cast<std::uint8_t>(code));
}

Payload copyPayload(const std::shared_ptr<::vsomeip::message>& msg) {
    Payload out;
    if (auto pl = msg->get_payload()) {
        const auto* p = pl->get_data();
        const auto n = pl->get_length();
        if (p != nullptr && n > 0) out.assign(p, p + n);
    }
    return out;
}

}  // namespace

// ============================================================
// State - owns every mutable aspect of the backend
// ============================================================
struct VsomeipBackend::State {
    struct PendingKey {
        ::vsomeip::service_t service;
        ::vsomeip::instance_t instance;
        ::vsomeip::method_t method;
        ::vsomeip::session_t session;
        bool operator<(const PendingKey& o) const {
            if (service != o.service) return service < o.service;
            if (instance != o.instance) return instance < o.instance;
            if (method != o.method) return method < o.method;
            return session < o.session;
        }
    };

    struct EventKey {
        ::vsomeip::service_t service;
        ::vsomeip::instance_t instance;
        ::vsomeip::event_t event;
        bool operator<(const EventKey& o) const {
            if (service != o.service) return service < o.service;
            if (instance != o.instance) return instance < o.instance;
            return event < o.event;
        }
    };

    struct Offered {
        InterfaceVersion version{};
        MethodDispatcher dispatcher;
        std::set<::vsomeip::event_t> events;
    };

    struct SubEntry {
        ServiceKey key{};
        EventgroupId group{0};
        EventId event{0};
        EventCallback cb;
        bool active{false};
    };

    struct Timer {
        std::uint64_t id;
        TimerCallback cb;
    };

    std::shared_ptr<Executor> executor;
    SomeIpConfig config;

    std::shared_ptr<::vsomeip::application> app;
    std::thread appThread;

    mutable std::mutex mutex;          // guards everything below except pending/timers
    std::condition_variable regCv;
    bool started{false};
    bool registered{false};

    std::map<ServiceKey, Offered> offered;
    std::map<ServiceKey, AvailabilityCallback> availability;
    std::map<ServiceKey, InterfaceVersion> requested;
    std::unordered_map<std::uint64_t, SubEntry> subs;
    std::map<EventKey, std::vector<std::uint64_t>> subsByEvent;
    std::uint64_t nextSubId{1};

    // One message handler per (service, instance), demultiplexed by message
    // type, so a process that both offers and consumes the same instance does
    // not clobber its own handler.
    std::set<ServiceKey> handlerRegistered;

    mutable std::mutex pendingMutex;   // held across app->send() + insert
    std::map<PendingKey, ResponseCallback> pending;

    mutable std::mutex timerMutex;
    std::condition_variable timerCv;
    std::multimap<std::chrono::steady_clock::time_point, Timer> timers;
    std::uint64_t nextTimerId{1};
    std::thread timerThread;
    bool timerStop{false};

    void post(std::function<void()> task) const {
        if (executor) executor->post(std::move(task));
    }

    // --- message demultiplexing (invoked on vsomeip threads) ---

    void onRequest(const std::shared_ptr<::vsomeip::message>& msg) {
        ServiceKey key{msg->get_service(), msg->get_instance()};
        MethodDispatcher dispatch;
        std::shared_ptr<::vsomeip::application> a;
        {
            std::lock_guard<std::mutex> lk(mutex);
            a = app;
            auto it = offered.find(key);
            if (it == offered.end() || !it->second.dispatcher) return;
            dispatch = it->second.dispatcher;
        }
        if (!a) return;

        // Copy the payload: the dispatcher runs later, on the Executor, and
        // must not alias a vsomeip-owned buffer.
        auto data = copyPayload(msg);
        const auto method = msg->get_method();
        const auto session = msg->get_session();
        const auto client = msg->get_client();
        const auto iface = msg->get_interface_version();

        post([a, msg, data = std::move(data), dispatch, key, method, session, client,
              iface]() {
            RequestContext ctx;
            ctx.service = key;
            ctx.method = method;
            ctx.session = session;
            ctx.clientId = client;
            ctx.requestId = std::to_string(static_cast<unsigned>(client)) + "/" +
                            std::to_string(static_cast<unsigned>(session));

            MethodResult result;
            try {
                PayloadView pv{data.empty() ? nullptr : data.data(), data.size()};
                result = dispatch(ctx, pv);
            } catch (...) {
                // The dispatcher threw: the peer gets a SOME/IP error response
                // and the exception text never reaches the wire (CR-005 §12).
                result = MethodResult::error(0x05, "handler error");
            }

            auto rt = ::vsomeip::runtime::get();
            if (!rt) return;
            auto response = rt->create_response(msg);
            if (!response) return;
            response->set_interface_version(iface);
            auto pl = rt->create_payload();
            if (result.success) {
                response->set_return_code(::vsomeip::return_code_e::E_OK);
                if (!result.response.empty()) {
                    pl->set_data(result.response.data(),
                                 static_cast<std::uint32_t>(result.response.size()));
                }
            } else {
                response->set_message_type(::vsomeip::message_type_e::MT_ERROR);
                response->set_return_code(toReturnCode(result.errorCode));
            }
            response->set_payload(pl);
            try {
                a->send(response);
            } catch (...) {
                // Could not hand the response to the stack; the peer's own call
                // timeout governs.
            }
        });
    }

    void onResponseMessage(const std::shared_ptr<::vsomeip::message>& msg) {
        PendingKey pk{msg->get_service(), msg->get_instance(), msg->get_method(),
                      msg->get_session()};
        ResponseCallback cb;
        {
            std::lock_guard<std::mutex> lk(pendingMutex);
            auto it = pending.find(pk);
            if (it == pending.end()) return;  // late or duplicate: discard
            cb = std::move(it->second);
            pending.erase(it);
        }
        if (!cb) return;

        const auto rc = msg->get_return_code();
        const bool isError =
            (msg->get_message_type() == ::vsomeip::message_type_e::MT_ERROR);
        auto data = copyPayload(msg);

        CallResult result = (!isError && rc == ::vsomeip::return_code_e::E_OK)
                                ? CallResult::ok(std::move(data))
                                : CallResult::fail(someipReturnCode(rc),
                                                   "remote returned an error");
        post([cb, result = std::move(result)]() mutable { cb(std::move(result)); });
    }

    void onNotification(const std::shared_ptr<::vsomeip::message>& msg) {
        EventKey ek{msg->get_service(), msg->get_instance(), msg->get_method()};
        std::vector<SubEntry> targets;
        {
            std::lock_guard<std::mutex> lk(mutex);
            auto it = subsByEvent.find(ek);
            if (it == subsByEvent.end()) return;
            for (auto id : it->second) {
                auto sit = subs.find(id);
                if (sit != subs.end() && sit->second.active && sit->second.cb) {
                    targets.push_back(sit->second);
                }
            }
        }
        if (targets.empty()) return;

        auto data = copyPayload(msg);
        for (auto& entry : targets) {
            auto buf = data;
            post([entry, buf = std::move(buf)]() {
                EventContext ctx;
                ctx.service = entry.key;
                ctx.event = entry.event;
                ctx.group = entry.group;
                PayloadView pv{buf.empty() ? nullptr : buf.data(), buf.size()};
                entry.cb(ctx, pv);
            });
        }
    }

    // Caller must hold `mutex`.
    void ensureMessageHandlerLocked(ServiceKey key) {
        if (handlerRegistered.count(key)) return;
        if (!app) return;
        handlerRegistered.insert(key);
        State* self = this;
        app->register_message_handler(
            key.service, key.instance, ::vsomeip::ANY_METHOD,
            [self](const std::shared_ptr<::vsomeip::message>& msg) {
                if (!msg) return;
                switch (msg->get_message_type()) {
                    case ::vsomeip::message_type_e::MT_REQUEST:
                    case ::vsomeip::message_type_e::MT_REQUEST_NO_RETURN:
                        self->onRequest(msg);
                        break;
                    case ::vsomeip::message_type_e::MT_RESPONSE:
                    case ::vsomeip::message_type_e::MT_ERROR:
                        self->onResponseMessage(msg);
                        break;
                    case ::vsomeip::message_type_e::MT_NOTIFICATION:
                        self->onNotification(msg);
                        break;
                    default:
                        break;
                }
            });
    }

    void timerLoop() {
        std::unique_lock<std::mutex> lk(timerMutex);
        while (!timerStop) {
            if (timers.empty()) {
                timerCv.wait(lk);
                continue;
            }
            const auto next = timers.begin()->first;
            timerCv.wait_until(lk, next);
            if (timerStop) break;
            const auto nowTp = std::chrono::steady_clock::now();
            std::vector<TimerCallback> due;
            while (!timers.empty() && timers.begin()->first <= nowTp) {
                due.push_back(std::move(timers.begin()->second.cb));
                timers.erase(timers.begin());
            }
            if (due.empty()) continue;
            lk.unlock();
            // Timer callbacks run on the Executor, like every other callback.
            for (auto& cb : due) {
                if (cb) post(cb);
            }
            lk.lock();
        }
    }
};

// ============================================================
// Construction / lifecycle
// ============================================================
VsomeipBackend::VsomeipBackend(std::shared_ptr<Executor> executor, SomeIpConfig config)
    : m_state(std::make_unique<State>()) {
    m_state->executor = std::move(executor);
    m_state->config = std::move(config);
}

VsomeipBackend::~VsomeipBackend() {
    try {
        stop();
    } catch (...) {
        // Never throw from a destructor (CCU-FW-0310).
    }
}

void VsomeipBackend::start() {
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        if (m_state->started) return;
    }

    auto rt = ::vsomeip::runtime::get();
    if (!rt) {
        throw SomeIpException("CCU-FW-0301", "start: vsomeip runtime unavailable");
    }

    auto app = rt->create_application(m_state->config.application);
    if (!app) {
        throw SomeIpException("CCU-FW-0301",
                              "start: create_application failed for identity '" +
                                  m_state->config.application + "'");
    }

    // init() parses the vsomeip configuration and allocates the client
    // identity. A false return is fail-closed: there is no fallback to a fake
    // backend in a production build.
    if (!app->init()) {
        throw SomeIpException("CCU-FW-0301",
                              "start: vsomeip init failed for identity '" +
                                  m_state->config.application + "'");
    }

    State* st = m_state.get();
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        m_state->app = app;
    }

    app->register_state_handler([st](::vsomeip::state_type_e state) {
        {
            std::lock_guard<std::mutex> lk(st->mutex);
            st->registered = (state == ::vsomeip::state_type_e::ST_REGISTERED);
        }
        st->regCv.notify_all();
    });

    {
        std::lock_guard<std::mutex> lk(m_state->timerMutex);
        m_state->timerStop = false;
    }
    m_state->timerThread = std::thread([st]() { st->timerLoop(); });

    // start() blocks until stop(); give it a dedicated thread.
    m_state->appThread = std::thread([app]() { app->start(); });

    // Bounded wait for routing-manager registration. A timeout is not fatal:
    // vsomeip keeps retrying, and any call issued before registration completes
    // is reported as CCU-FW-0305 rather than dropped silently.
    {
        std::unique_lock<std::mutex> lk(m_state->mutex);
        m_state->regCv.wait_for(lk, kRegistrationWait,
                                [st]() { return st->registered; });
        m_state->started = true;
    }
}

void VsomeipBackend::stop() {
    std::shared_ptr<::vsomeip::application> app;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        if (!m_state->started && !m_state->app) return;
        app = m_state->app;
        m_state->started = false;
        m_state->registered = false;
    }

    // Fail every outstanding call before teardown so no caller is left waiting
    // on a promise that will never be satisfied (CR-005 §7).
    std::vector<ResponseCallback> orphans;
    {
        std::lock_guard<std::mutex> lk(m_state->pendingMutex);
        for (auto& kv : m_state->pending) orphans.push_back(std::move(kv.second));
        m_state->pending.clear();
    }
    for (auto& cb : orphans) {
        if (!cb) continue;
        m_state->post([cb]() {
            cb(CallResult::fail("CCU-FW-0305", "call aborted: backend stopping"));
        });
    }

    {
        std::lock_guard<std::mutex> lk(m_state->timerMutex);
        m_state->timerStop = true;
        m_state->timers.clear();
    }
    m_state->timerCv.notify_all();
    if (m_state->timerThread.joinable()) m_state->timerThread.join();

    if (app) {
        try {
            app->clear_all_handler();
            app->stop();
        } catch (...) {
            // Best-effort teardown (CCU-FW-0310).
        }
    }
    if (m_state->appThread.joinable()) m_state->appThread.join();

    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        m_state->app.reset();
        m_state->offered.clear();
        m_state->availability.clear();
        m_state->requested.clear();
        m_state->subs.clear();
        m_state->subsByEvent.clear();
        m_state->handlerRegistered.clear();
    }
}

// ============================================================
// Provider operations
// ============================================================
void VsomeipBackend::offerService(ServiceKey key, InterfaceVersion ver) {
    std::shared_ptr<::vsomeip::application> app;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        app = m_state->app;
        if (!app) throw SomeIpException("CCU-FW-0303", "offerService: backend not started");
        if (!m_state->offered.count(key) &&
            m_state->offered.size() >= m_state->config.maxProviders) {
            throw SomeIpException("CCU-FW-0309", "offerService: provider limit exhausted");
        }
        m_state->offered[key].version = ver;
        m_state->ensureMessageHandlerLocked(key);
    }
    try {
        app->offer_service(key.service, key.instance, ver.major, ver.minor);
    } catch (...) {
        throw SomeIpException("CCU-FW-0303", "offerService: vsomeip offer failed");
    }
}

void VsomeipBackend::stopOfferService(ServiceKey key) {
    std::shared_ptr<::vsomeip::application> app;
    std::set<::vsomeip::event_t> events;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        app = m_state->app;
        auto it = m_state->offered.find(key);
        if (it != m_state->offered.end()) {
            events = it->second.events;
            m_state->offered.erase(it);
        }
    }
    if (!app) return;
    try {
        for (auto ev : events) app->stop_offer_event(key.service, key.instance, ev);
        app->stop_offer_service(key.service, key.instance);
    } catch (...) {
        // Withdrawal is best-effort; local state is already cleared.
    }
}

void VsomeipBackend::setMethodDispatcher(ServiceKey key, MethodDispatcher dispatch) {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    if (!m_state->app) {
        throw SomeIpException("CCU-FW-0303", "setMethodDispatcher: backend not started");
    }
    m_state->offered[key].dispatcher = std::move(dispatch);
    m_state->ensureMessageHandlerLocked(key);
}

void VsomeipBackend::offerEvent(ServiceKey key, EventId event, EventgroupId group,
                                EventOptions opts) {
    std::shared_ptr<::vsomeip::application> app;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        app = m_state->app;
        if (!app) throw SomeIpException("CCU-FW-0308", "offerEvent: backend not started");
        m_state->offered[key].events.insert(event);
    }
    try {
        std::set<::vsomeip::eventgroup_t> groups{group};
        app->offer_event(key.service, key.instance, event, groups,
                         ::vsomeip::event_type_e::ET_EVENT,
                         std::chrono::milliseconds::zero(), false, true, nullptr,
                         opts.reliable ? ::vsomeip::reliability_type_e::RT_RELIABLE
                                       : ::vsomeip::reliability_type_e::RT_UNRELIABLE);
    } catch (...) {
        throw SomeIpException("CCU-FW-0308", "offerEvent: vsomeip offer_event failed");
    }
}

void VsomeipBackend::stopOfferEvent(ServiceKey key, EventId event) {
    std::shared_ptr<::vsomeip::application> app;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        app = m_state->app;
        auto it = m_state->offered.find(key);
        if (it != m_state->offered.end()) it->second.events.erase(event);
    }
    if (!app) return;
    try {
        app->stop_offer_event(key.service, key.instance, event);
    } catch (...) {
    }
}

void VsomeipBackend::notify(ServiceKey key, EventId event, PayloadView payload) {
    if (payload.size > 0 && payload.data == nullptr) {
        throw SomeIpException("CCU-FW-0306", "notify: illegal payload view");
    }
    if (payload.size > m_state->config.maxPayloadBytes) {
        throw SomeIpException("CCU-FW-0306", "notify: payload exceeds configured maximum");
    }
    std::shared_ptr<::vsomeip::application> app;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        app = m_state->app;
    }
    if (!app) throw SomeIpException("CCU-FW-0308", "notify: backend not started");
    auto rt = ::vsomeip::runtime::get();
    if (!rt) throw SomeIpException("CCU-FW-0308", "notify: vsomeip runtime unavailable");
    auto pl = rt->create_payload();
    if (payload.size > 0) {
        pl->set_data(payload.data, static_cast<std::uint32_t>(payload.size));
    }
    try {
        app->notify(key.service, key.instance, event, pl);
    } catch (...) {
        throw SomeIpException("CCU-FW-0308", "notify: vsomeip notify failed");
    }
}

// ============================================================
// Client operations
// ============================================================
void VsomeipBackend::requestService(ServiceKey key, InterfaceVersion ver,
                                    AvailabilityCallback cb) {
    std::shared_ptr<::vsomeip::application> app;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        app = m_state->app;
        if (!app) throw SomeIpException("CCU-FW-0303", "requestService: backend not started");
        if (!m_state->requested.count(key) &&
            m_state->requested.size() >= m_state->config.maxClients) {
            throw SomeIpException("CCU-FW-0309", "requestService: client limit exhausted");
        }
        m_state->availability[key] = std::move(cb);
        m_state->requested[key] = ver;
        m_state->ensureMessageHandlerLocked(key);
    }

    State* st = m_state.get();
    try {
        // ANY_MINOR for the availability handler: SOME/IP-SD instance matching
        // is major-versioned, and pinning the minor here would silently hide an
        // otherwise compatible offer.
        app->register_availability_handler(
            key.service, key.instance,
            [st](::vsomeip::service_t service, ::vsomeip::instance_t instance,
                 bool available) {
                ServiceKey k{service, instance};
                AvailabilityCallback acb;
                {
                    std::lock_guard<std::mutex> lk(st->mutex);
                    auto it = st->availability.find(k);
                    if (it == st->availability.end() || !it->second) return;
                    acb = it->second;
                    // The stack re-arms subscriptions on reconnect; a
                    // cancelled subscription is never restored (CR-005 §7/§8).
                    for (auto& kv : st->subs) {
                        if (kv.second.key == k) kv.second.active = available;
                    }
                }
                st->post([acb, available]() {
                    acb(available ? Availability::Available : Availability::Unavailable);
                });
            },
            ver.major, ::vsomeip::ANY_MINOR);
        app->request_service(key.service, key.instance, ver.major, ver.minor);
    } catch (...) {
        throw SomeIpException("CCU-FW-0303", "requestService: vsomeip request failed");
    }
}

void VsomeipBackend::releaseService(ServiceKey key) {
    std::shared_ptr<::vsomeip::application> app;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        app = m_state->app;
        m_state->availability.erase(key);
        m_state->requested.erase(key);
    }
    if (!app) return;
    try {
        app->unregister_availability_handler(key.service, key.instance);
        app->release_service(key.service, key.instance);
    } catch (...) {
    }
}

void VsomeipBackend::sendRequest(ServiceKey target, MethodId method, PayloadView payload,
                                 SessionId /*session*/, std::uint16_t /*clientId*/,
                                 const std::string& /*traceId*/,
                                 const CallOptions& /*opts*/,
                                 ResponseCallback onResponse) {
    // The framework-level session/clientId/traceId are the caller's own
    // correlation keys; the wire session is assigned by vsomeip below. The call
    // timeout is owned by the caller via scheduleTimer (CR-005 §7).
    if (payload.size > 0 && payload.data == nullptr) {
        throw SomeIpException("CCU-FW-0306", "sendRequest: illegal payload view");
    }
    if (payload.size > m_state->config.maxPayloadBytes) {
        throw SomeIpException("CCU-FW-0306",
                              "sendRequest: payload exceeds configured maximum");
    }

    std::shared_ptr<::vsomeip::application> app;
    InterfaceVersion ver{};
    bool isRegistered = false;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        app = m_state->app;
        isRegistered = m_state->registered;
        auto it = m_state->requested.find(target);
        if (it != m_state->requested.end()) ver = it->second;
    }
    {
        std::lock_guard<std::mutex> lk(m_state->pendingMutex);
        if (m_state->pending.size() >= m_state->config.maxInflightCalls) {
            throw SomeIpException("CCU-FW-0309", "sendRequest: in-flight limit exhausted");
        }
    }

    auto failFast = [this, &onResponse](std::string msg) {
        if (!onResponse) return;
        auto cb = onResponse;
        m_state->post([cb, msg = std::move(msg)]() {
            cb(CallResult::fail("CCU-FW-0305", msg));
        });
    };

    // Not yet attached to the routing manager: surface a transport failure so
    // the caller's retry/timeout policy applies instead of dropping silently.
    if (!app || !isRegistered) {
        failFast("send: backend not registered with routing manager");
        return;
    }
    auto rt = ::vsomeip::runtime::get();
    if (!rt) {
        failFast("send: vsomeip runtime unavailable");
        return;
    }
    auto request = rt->create_request();
    if (!request) {
        failFast("send: request allocation failed");
        return;
    }
    request->set_service(target.service);
    request->set_instance(target.instance);
    request->set_method(method);
    request->set_interface_version(ver.major);
    auto pl = rt->create_payload();
    if (payload.size > 0) {
        pl->set_data(payload.data, static_cast<std::uint32_t>(payload.size));
    }
    request->set_payload(pl);

    bool sendOk = true;
    {
        std::lock_guard<std::mutex> lk(m_state->pendingMutex);
        try {
            app->send(request);
        } catch (...) {
            sendOk = false;
        }
        if (sendOk) {
            m_state->pending[State::PendingKey{target.service, target.instance, method,
                                               request->get_session()}] =
                std::move(onResponse);
        }
    }
    // Post the failure callback outside the lock.
    if (!sendOk) failFast("send: vsomeip send failed");
}

std::uint64_t VsomeipBackend::subscribe(ServiceKey key, EventgroupId group, EventId event,
                                        EventCallback cb) {
    std::shared_ptr<::vsomeip::application> app;
    InterfaceVersion ver{};
    std::uint64_t id = 0;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        app = m_state->app;
        if (!app) throw SomeIpException("CCU-FW-0308", "subscribe: backend not started");
        if (m_state->subs.size() >= m_state->config.maxSubscriptionsPerClient) {
            throw SomeIpException("CCU-FW-0309", "subscribe: subscription limit exhausted");
        }
        auto it = m_state->requested.find(key);
        if (it != m_state->requested.end()) ver = it->second;
        id = m_state->nextSubId++;
        State::SubEntry e;
        e.key = key;
        e.group = group;
        e.event = event;
        e.cb = std::move(cb);
        e.active = true;
        m_state->subs[id] = std::move(e);
        m_state->subsByEvent[State::EventKey{key.service, key.instance, event}]
            .push_back(id);
        m_state->ensureMessageHandlerLocked(key);
    }
    try {
        std::set<::vsomeip::eventgroup_t> groups{group};
        app->request_event(key.service, key.instance, event, groups,
                           ::vsomeip::event_type_e::ET_EVENT,
                           ::vsomeip::reliability_type_e::RT_UNKNOWN);
        app->subscribe(key.service, key.instance, group, ver.major, event);
    } catch (...) {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        m_state->subs.erase(id);
        auto ek = State::EventKey{key.service, key.instance, event};
        auto eit = m_state->subsByEvent.find(ek);
        if (eit != m_state->subsByEvent.end()) {
            auto& v = eit->second;
            v.erase(std::remove(v.begin(), v.end(), id), v.end());
            if (v.empty()) m_state->subsByEvent.erase(eit);
        }
        throw SomeIpException("CCU-FW-0308", "subscribe: vsomeip subscribe failed");
    }
    return id;
}

void VsomeipBackend::unsubscribe(std::uint64_t subId) {
    std::shared_ptr<::vsomeip::application> app;
    ServiceKey key{};
    EventgroupId group = 0;
    EventId event = 0;
    bool lastForEvent = false;
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        app = m_state->app;
        auto it = m_state->subs.find(subId);
        if (it == m_state->subs.end()) return;
        key = it->second.key;
        group = it->second.group;
        event = it->second.event;
        m_state->subs.erase(it);

        auto ek = State::EventKey{key.service, key.instance, event};
        auto eit = m_state->subsByEvent.find(ek);
        if (eit != m_state->subsByEvent.end()) {
            auto& v = eit->second;
            v.erase(std::remove(v.begin(), v.end(), subId), v.end());
            if (v.empty()) {
                m_state->subsByEvent.erase(eit);
                lastForEvent = true;
            }
        }
    }
    // Only withdraw from the stack once the last local subscriber is gone.
    if (!app || !lastForEvent) return;
    try {
        app->unsubscribe(key.service, key.instance, group);
        app->release_event(key.service, key.instance, event);
    } catch (...) {
    }
}

// ============================================================
// Timers / clock
// ============================================================
std::uint64_t VsomeipBackend::scheduleTimer(std::chrono::milliseconds delay,
                                            TimerCallback cb) {
    std::uint64_t id;
    {
        std::lock_guard<std::mutex> lk(m_state->timerMutex);
        if (m_state->timers.size() >= m_state->config.maxRetryTimers) {
            throw SomeIpException("CCU-FW-0309", "scheduleTimer: timer limit exhausted");
        }
        id = m_state->nextTimerId++;
        m_state->timers.emplace(std::chrono::steady_clock::now() + delay,
                                State::Timer{id, std::move(cb)});
    }
    m_state->timerCv.notify_all();
    return id;
}

void VsomeipBackend::cancelTimer(std::uint64_t timerId) {
    {
        std::lock_guard<std::mutex> lk(m_state->timerMutex);
        for (auto it = m_state->timers.begin(); it != m_state->timers.end(); ++it) {
            if (it->second.id == timerId) {
                m_state->timers.erase(it);
                break;
            }
        }
    }
    m_state->timerCv.notify_all();
}

std::chrono::milliseconds VsomeipBackend::now() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch());
}

// ============================================================
// Observability
// ============================================================
bool VsomeipBackend::started() const {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    return m_state->started;
}

bool VsomeipBackend::registered() const {
    std::lock_guard<std::mutex> lk(m_state->mutex);
    return m_state->registered;
}

std::size_t VsomeipBackend::inFlight() const {
    std::lock_guard<std::mutex> lk(m_state->pendingMutex);
    return m_state->pending.size();
}

std::size_t VsomeipBackend::pendingTimers() const {
    std::lock_guard<std::mutex> lk(m_state->timerMutex);
    return m_state->timers.size();
}

}  // namespace detail
}  // namespace ccu::fw::someip
