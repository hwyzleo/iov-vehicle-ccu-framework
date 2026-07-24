#include "log.h"
#include "log_enricher.h"
#include "log_redactor.h"
#include "log_level_filter.h"
#include "log_json_formatter.h"
#include "log_async_dispatcher.h"
#include "log_sink_manager.h"
#include "log_emergency_writer.h"
#include <unordered_map>
#include <mutex>
#include <vector>
#include <cstdlib>

namespace cgw {
namespace fw {
namespace log {

// ============================================================
// 线程局部上下文
// ============================================================
static thread_local const LogContext* t_context = nullptr;

const LogContext* ContextScope::current() {
    return t_context;
}

ContextScope::ContextScope(LogContext context)
    : m_context(std::move(context))
    , m_previous(t_context)
{
    t_context = &m_context;
}

ContextScope::~ContextScope() {
    t_context = m_previous;
}

// ============================================================
// LoggerRegistry
// ============================================================
class LoggerRegistry {
public:
    static LoggerRegistry& instance() {
        static LoggerRegistry inst;
        return inst;
    }

    InitResult init(const std::string& service, const LogConfig& config) {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_initialized) {
            return {LogError::kOk, "Already initialized"};
        }

        m_service = service;
        m_enricher.reset(new Enricher(service));
        m_redactor.reset(new Redactor(config.redact_config));
        m_levelFilter.reset(new LevelFilter(config));
        m_sinkManager.reset(new SinkManager(config, service));

        if (config.async_config.enabled) {
            auto writer = [this](const std::string& line, bool isError) -> bool {
                return m_sinkManager->write(line, isError);
            };
            m_dispatcher.reset(new AsyncDispatcher(
                config.async_config.queue_size,
                config.async_config.flush_interval_ms,
                std::move(writer)
            ));
            m_dispatcher->start();
        }

        m_initialized = true;
        return {LogError::kOk, ""};
    }

    Logger getLogger(const std::string& module) {
        Logger logger;
        logger.m_impl = std::make_shared<Logger::Impl>(
            module, m_enricher.get(), m_redactor.get(),
            m_levelFilter.get(), m_dispatcher.get(), m_sinkManager.get()
        );
        return logger;
    }

    bool isInitialized() const { return m_initialized; }

    void shutdown() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_dispatcher) {
            m_dispatcher->stop();
        }
        m_initialized = false;
    }

private:
    LoggerRegistry() = default;

    std::mutex m_mutex;
    bool m_initialized = false;
    std::string m_service;
    std::unique_ptr<Enricher> m_enricher;
    std::unique_ptr<Redactor> m_redactor;
    std::unique_ptr<LevelFilter> m_levelFilter;
    std::unique_ptr<AsyncDispatcher> m_dispatcher;
    std::unique_ptr<SinkManager> m_sinkManager;
};

// ============================================================
// Logger::Impl
// ============================================================
class Logger::Impl {
public:
    Impl(const std::string& module,
         Enricher* enricher,
         Redactor* redactor,
         LevelFilter* levelFilter,
         AsyncDispatcher* dispatcher,
         SinkManager* sinkManager)
        : m_module(module)
        , m_enricher(enricher)
        , m_redactor(redactor)
        , m_levelFilter(levelFilter)
        , m_dispatcher(dispatcher)
        , m_sinkManager(sinkManager)
    {}

    void log(LogLevel level, std::string_view event, std::string_view message,
             std::initializer_list<Field> fields) {
        if (!m_levelFilter->shouldLog(level, m_module)) {
            return;
        }

        std::vector<Field> fieldVec(fields.begin(), fields.end());
        const LogContext* ctx = ContextScope::current();

        std::vector<Field> enriched = m_enricher->enrich(
            std::move(fieldVec), level, m_module,
            std::string(event), std::string(message), ctx
        );

        std::vector<Field> redacted = m_redactor->redact(std::move(enriched));
        std::string jsonLine = JsonLineFormatter::format(redacted);

        if (m_dispatcher) {
            m_dispatcher->submit(jsonLine, level);
        } else {
            m_sinkManager->write(jsonLine, level >= LogLevel::kError);
        }
    }

    void flush() {
        if (m_dispatcher) {
            m_dispatcher->flush();
        }
        m_sinkManager->flush();
    }

private:
    std::string m_module;
    Enricher* m_enricher;
    Redactor* m_redactor;
    LevelFilter* m_levelFilter;
    AsyncDispatcher* m_dispatcher;
    SinkManager* m_sinkManager;
};

// ============================================================
// Logger 公共 API 实现
// ============================================================

Logger::Logger() = default;

InitResult Logger::init(const std::string& service, const LogConfig& config) {
    return LoggerRegistry::instance().init(service, config);
}

Logger Logger::get(const std::string& module) {
    return LoggerRegistry::instance().getLogger(module);
}

void Logger::trace(std::string_view event, std::string_view message,
                   std::initializer_list<Field> fields) {
    if (m_impl) m_impl->log(LogLevel::kTrace, event, message, fields);
}

void Logger::debug(std::string_view event, std::string_view message,
                   std::initializer_list<Field> fields) {
    if (m_impl) m_impl->log(LogLevel::kDebug, event, message, fields);
}

void Logger::info(std::string_view event, std::string_view message,
                  std::initializer_list<Field> fields) {
    if (m_impl) m_impl->log(LogLevel::kInfo, event, message, fields);
}

void Logger::warn(std::string_view event, std::string_view message,
                  std::initializer_list<Field> fields) {
    if (m_impl) m_impl->log(LogLevel::kWarn, event, message, fields);
}

void Logger::error(std::string_view event, std::string_view message,
                   std::initializer_list<Field> fields) {
    if (m_impl) m_impl->log(LogLevel::kError, event, message, fields);
}

void Logger::fatal(std::string_view event, std::string_view message,
                   std::initializer_list<Field> fields) {
    if (m_impl) {
        m_impl->log(LogLevel::kFatal, event, message, fields);
        m_impl->flush();
    }
    std::abort();
}

void Logger::flush() {
    if (m_impl) m_impl->flush();
}

} // namespace log
} // namespace fw
} // namespace cgw
