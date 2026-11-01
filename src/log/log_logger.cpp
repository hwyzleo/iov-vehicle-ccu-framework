#include "log.h"
#include "log_enricher.h"
#include "log_redactor.h"
#include "log_level_filter.h"
#include "log_json_formatter.h"
#include "log_standard_formatter.h"
#include "log_async_dispatcher.h"
#include "log_sink_manager.h"
#include "log_emergency_writer.h"
#include <unordered_map>
#include <mutex>
#include <vector>
#include <cstdlib>

namespace ccu {
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
        m_useStandardFormat = (config.format == "standard");

        if (config.async_config.enabled) {
            auto writer = [this](const std::string& line, LogLevel level) -> bool {
                return m_sinkManager->write(line, level);
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
        if (!m_initialized) {
            // 日志系统未初始化，返回 no-op Logger（m_impl 为空）
            // Logger 的所有输出方法已有 if (m_impl) 保护，不会崩溃
            return Logger();
        }
        Logger logger;
        logger.m_impl = std::make_shared<Logger::Impl>(
            module, m_enricher.get(), m_redactor.get(),
            m_levelFilter.get(), m_dispatcher.get(), m_sinkManager.get(),
            m_useStandardFormat
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

    // 显式析构：在成员按声明逆序销毁之前，先停止异步派发线程并 join。
    // 否则（静态销毁阶段）m_sinkManager 会先于 m_dispatcher 被销毁，
    // 而仍在运行的 worker 线程会通过 writer 回调访问已释放的 SinkManager，
    // 造成对已销毁 sink 的悬垂访问（EXC_BAD_ACCESS）。
    ~LoggerRegistry() {
        if (m_dispatcher) {
            m_dispatcher->stop();
        }
    }

    std::mutex m_mutex;
    bool m_initialized = false;
    std::string m_service;
    std::unique_ptr<Enricher> m_enricher;
    std::unique_ptr<Redactor> m_redactor;
    std::unique_ptr<LevelFilter> m_levelFilter;
    std::unique_ptr<AsyncDispatcher> m_dispatcher;
    std::unique_ptr<SinkManager> m_sinkManager;
    bool m_useStandardFormat = true;
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
         SinkManager* sinkManager,
         bool useStandardFormat)
        : m_module(module)
        , m_enricher(enricher)
        , m_redactor(redactor)
        , m_levelFilter(levelFilter)
        , m_dispatcher(dispatcher)
        , m_sinkManager(sinkManager)
        , m_useStandardFormat(useStandardFormat)
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

        std::string line;
        if (m_useStandardFormat) {
            line = StandardFormatter::format(redacted);
        } else {
            line = JsonLineFormatter::format(redacted);
        }

        if (m_dispatcher) {
            m_dispatcher->submit(line, level);
        } else {
            m_sinkManager->write(line, level);
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
    bool m_useStandardFormat = true;
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
} // namespace ccu
