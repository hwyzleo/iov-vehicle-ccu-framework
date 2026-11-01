// src/someip_runtime.cpp - RuntimeImpl, validation helpers, default Executor
// (CR-005 §4/§5/§10/§11/§13).

#include "someip_impl.h"

#if defined(CCU_FW_HAVE_VSOMEIP)
// Production backend (CR-005 §15). Compiled in only when the platform build
// enables CCU_FW_SOMEIP_VSOMEIP, so HOST builds carry no vsomeip dependency.
#include "someip_vsomeip_backend.h"
#endif

#include <queue>
#include <set>
#include <thread>

namespace ccu::fw::someip {
namespace detail {

namespace {

// Process-wide application identity registry (CR-005 §5). Duplicate identity
// before start yields CCU-FW-0302.
std::mutex& identityMutex() {
    static std::mutex m;
    return m;
}
std::set<std::string>& identityRegistry() {
    static std::set<std::string> s;
    return s;
}

void throwBad(std::string code, const std::string& what) {
    throw SomeIpException(std::move(code), what);
}

}  // namespace

// ============================================================
// Validation helpers (CR-005 §4/§10)
// ============================================================
void validateServiceKey(ServiceKey key) {
    if (key.service == 0x0000 || key.service == 0xFFFF) {
        throwBad("CCU-FW-0302", "validateServiceKey: reserved service id");
    }
    if (key.instance == 0xFFFF) {
        throwBad("CCU-FW-0302", "validateServiceKey: reserved instance id (wildcard)");
    }
}

void validateMethodId(MethodId id) {
    if (id == 0x0000 || id >= 0x8000) {
        throwBad("CCU-FW-0302", "validateMethodId: reserved/out-of-range method id");
    }
}

void validateEventId(EventId id) {
    if (id < 0x8000 || id == 0xFFFF) {
        throwBad("CCU-FW-0302", "validateEventId: out-of-range event id");
    }
}

void validateEventgroupId(EventgroupId id) {
    if (id == 0x0000 || id == 0xFFFF) {
        throwBad("CCU-FW-0302", "validateEventgroupId: reserved eventgroup id");
    }
}

void validatePayload(PayloadView pv, std::size_t maxBytes) {
    if (pv.size > 0 && pv.data == nullptr) {
        throwBad("CCU-FW-0306", "validatePayload: non-zero length with null data pointer");
    }
    if (pv.size > maxBytes) {
        throwBad("CCU-FW-0306", "validatePayload: payload exceeds max bytes");
    }
}

void validateConfig(const SomeIpConfig& cfg) {
    if (cfg.application.empty()) {
        throwBad("CCU-FW-0302", "validateConfig: application identity empty");
    }
    if (cfg.maxPayloadBytes == 0 || cfg.maxInflightCalls == 0 ||
        cfg.callbackQueueSize == 0 || cfg.maxProviders == 0 ||
        cfg.maxClients == 0 || cfg.maxSubscriptionsPerClient == 0 ||
        cfg.maxRetryTimers == 0) {
        throwBad("CCU-FW-0302", "validateConfig: resource limit must be positive");
    }
    if (cfg.shutdownTimeout.count() <= 0 || cfg.callTimeout.count() <= 0) {
        throwBad("CCU-FW-0302", "validateConfig: timeout must be positive");
    }
    const auto& d = cfg.discovery;
    if (d.enabled) {
        if (d.initialBackoff.count() <= 0) {
            throwBad("CCU-FW-0302", "validateConfig: initial backoff must be positive");
        }
        if (d.maxBackoff < d.initialBackoff) {
            throwBad("CCU-FW-0302", "validateConfig: max backoff < initial backoff");
        }
        if (d.multiplier < 1.0) {
            throwBad("CCU-FW-0302", "validateConfig: multiplier must be >= 1.0");
        }
        if (d.jitterPercent < 0 || d.jitterPercent > 100) {
            throwBad("CCU-FW-0302", "validateConfig: jitter percent out of range");
        }
    }
}

// ============================================================
// Default bounded worker-pool Executor (CR-005 §11)
// ============================================================
namespace {

class ThreadPoolExecutor : public Executor {
public:
    explicit ThreadPoolExecutor(std::size_t n) : m_stop(false) {
        if (n == 0) n = 2;
        m_workers.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            m_workers.emplace_back([this]() { worker(); });
        }
    }
    ~ThreadPoolExecutor() override { shutdown(); }

    void post(std::function<void()> task) override {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (m_stop) return;
            m_tasks.push(std::move(task));
        }
        m_cv.notify_one();
    }

    void shutdown() override {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if (m_stop) return;
            m_stop = true;
        }
        m_cv.notify_all();
        for (auto& t : m_workers) {
            if (t.joinable()) t.join();
        }
    }

private:
    void worker() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(m_mutex);
                m_cv.wait(lk, [this]() { return m_stop || !m_tasks.empty(); });
                if (m_stop && m_tasks.empty()) return;
                task = std::move(m_tasks.front());
                m_tasks.pop();
            }
            task();
        }
    }

    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop;
};

}  // namespace

// ============================================================
// RuntimeImpl
// ============================================================
RuntimeImpl::RuntimeImpl(SomeIpConfig config, std::shared_ptr<Executor> executor,
                         std::shared_ptr<DiagnosticsSink> diag)
    : RuntimeImpl(std::move(config), std::move(executor), std::move(diag), nullptr) {}

RuntimeImpl::RuntimeImpl(SomeIpConfig config, std::shared_ptr<Executor> executor,
                         std::shared_ptr<DiagnosticsSink> diag,
                         std::shared_ptr<SomeIpBackend> backend)
    : m_config(std::move(config))
    , m_executor(std::move(executor))
    , m_diag(std::move(diag))
    , m_backend(std::move(backend))
    , m_application(m_config.application) {
    validateConfig(m_config);
    if (!m_executor) {
        unsigned n = std::thread::hardware_concurrency();
        if (n == 0) n = 2;
        if (n > 4) n = 4;
        m_executor = std::make_shared<ThreadPoolExecutor>(n);
    }
    if (!m_backend) {
        // Backend selection (CR-005 §15). A build configured with
        // CCU_FW_SOMEIP_VSOMEIP=ON gets the platform-approved production
        // backend; otherwise the deterministic no-network fake is used. HOST
        // tests are unaffected either way: they inject FakeSomeIpBackend
        // explicitly through detail::makeTestRuntime.
#if defined(CCU_FW_HAVE_VSOMEIP)
        m_backend = std::make_shared<VsomeipBackend>(m_executor, m_config);
#else
        m_backend = std::make_shared<FakeSomeIpBackend>(m_executor);
#endif
    }
    // Identity uniqueness: duplicate application before start -> CCU-FW-0302.
    if (!registerIdentity(m_application)) {
        throwBad("CCU-FW-0302", "create: duplicate application identity: " + m_application);
    }
    m_identityRegistered = true;
}

RuntimeImpl::~RuntimeImpl() {
    try {
        stop();
    } catch (...) {
        // Best-effort cleanup (CCU-FW-0310); never throw from destructor.
    }
    if (m_identityRegistered) {
        unregisterIdentity(m_application);
    }
}

void RuntimeImpl::start() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_state == RuntimeState::Running) return;
        if (m_state != RuntimeState::Created) {
            throwBad("CCU-FW-0301", "start: invalid runtime state");
        }
        m_state = RuntimeState::Starting;
    }
    try {
        m_backend->start();
    } catch (const SomeIpException&) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_state = RuntimeState::Created;
        throw;
    } catch (const std::exception&) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_state = RuntimeState::Created;
        throwBad("CCU-FW-0301", "start: backend start failed");
    }
    std::lock_guard<std::mutex> lk(m_mutex);
    m_state = RuntimeState::Running;
}

void RuntimeImpl::stop() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_state == RuntimeState::Stopping) return;  // concurrent stop idempotent
        if (m_state == RuntimeState::Stopped) return;   // already stopped
        if (m_state == RuntimeState::Created) {
            m_state = RuntimeState::Stopped;  // never started: straight to stopped
            return;
        }
        m_state = RuntimeState::Stopping;
    }
    // Withdraw, fire Stopping callbacks, clear backend state, drain executor
    // (CR-005 §11). Best-effort on backend/executor failure (CCU-FW-0310).
    try {
        m_backend->stop();
    } catch (...) { /* reported via state, not rethrown */ }
    if (m_executor) {
        try {
            m_executor->shutdown();
        } catch (...) { /* best-effort */ }
    }
    std::lock_guard<std::mutex> lk(m_mutex);
    m_state = RuntimeState::Stopped;
}

bool RuntimeImpl::running() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_state == RuntimeState::Running;
}

RuntimeState RuntimeImpl::state() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_state;
}

std::shared_ptr<ProviderImpl> RuntimeImpl::createProvider(ServiceKey key,
                                                          InterfaceVersion ver) {
    validateServiceKey(key);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_providerCount >= m_config.maxProviders) {
            throwBad("CCU-FW-0309", "createProvider: provider limit exhausted");
        }
        ++m_providerCount;
    }
    return std::make_shared<ProviderImpl>(shared_from_this(), key, ver);
}

std::shared_ptr<ClientImpl> RuntimeImpl::createClient(ServiceKey key,
                                                      InterfaceVersion ver) {
    validateServiceKey(key);
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_clientCount >= m_config.maxClients) {
            throwBad("CCU-FW-0309", "createClient: client limit exhausted");
        }
        ++m_clientCount;
    }
    return std::make_shared<ClientImpl>(shared_from_this(), key, ver);
}

void RuntimeImpl::registerProvider() {
    std::lock_guard<std::mutex> lk(m_mutex);
    ++m_providerCount;
}

void RuntimeImpl::unregisterProvider() {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_providerCount > 0) --m_providerCount;
}

void RuntimeImpl::registerClient() {
    std::lock_guard<std::mutex> lk(m_mutex);
    ++m_clientCount;
}

void RuntimeImpl::unregisterClient() {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_clientCount > 0) --m_clientCount;
}

void RuntimeImpl::report(const DiagnosticsEvent& ev) {
    if (m_diag) m_diag->report(ev);
}

bool RuntimeImpl::registerIdentity(const std::string& application) {
    std::lock_guard<std::mutex> lk(identityMutex());
    if (identityRegistry().count(application)) return false;
    identityRegistry().insert(application);
    return true;
}

void RuntimeImpl::unregisterIdentity(const std::string& application) {
    std::lock_guard<std::mutex> lk(identityMutex());
    identityRegistry().erase(application);
}

// ============================================================
// SomeIpRuntime facade (in ccu::fw::someip, not detail)
// ============================================================
}  // namespace detail

SomeIpRuntime SomeIpRuntime::create(const SomeIpConfig& config,
                                    std::shared_ptr<Executor> executor,
                                    std::shared_ptr<DiagnosticsSink> diag) {
    auto impl = std::make_shared<detail::RuntimeImpl>(config, std::move(executor),
                                              std::move(diag));
    return SomeIpRuntime(std::move(impl));
}

void SomeIpRuntime::start() { m_impl->start(); }
void SomeIpRuntime::stop() { m_impl->stop(); }
bool SomeIpRuntime::running() const { return m_impl->running(); }
RuntimeState SomeIpRuntime::state() const { return m_impl->state(); }

Provider SomeIpRuntime::createProvider(ServiceKey key, InterfaceVersion version) {
    return Provider(m_impl->createProvider(key, version));
}

Client SomeIpRuntime::createClient(ServiceKey key, InterfaceVersion version) {
    return Client(m_impl->createClient(key, version));
}

}  // namespace ccu::fw::someip
