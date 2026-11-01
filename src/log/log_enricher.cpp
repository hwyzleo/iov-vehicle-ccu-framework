#include "log_enricher.h"
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <ctime>
#include <cstdio>

// gettid() 在 macOS 上不可用，需要兼容处理
#ifdef __APPLE__
#include <pthread.h>
#endif

namespace ccu {
namespace fw {
namespace log {

namespace {
pid_t current_tid() {
#ifdef __APPLE__
    uint64_t tid;
    pthread_threadid_np(nullptr, &tid);
    return static_cast<pid_t>(tid);
#else
    return gettid();
#endif
}
} // anonymous namespace

Enricher::Enricher(const std::string& service)
    : m_service(service)
    , m_startTime(std::chrono::steady_clock::now())
    , m_pid(getpid())
{
}

std::vector<Field> Enricher::enrich(
    std::vector<Field> fields,
    LogLevel level,
    const std::string& module,
    const std::string& event,
    const std::string& message,
    const LogContext* context
) const {
    std::vector<Field> enriched;
    enriched.reserve(fields.size() + 12);

    enriched.push_back({"schema_version", FieldValue::makeInt(1)});
    enriched.push_back({"timestamp", FieldValue::makeString(getTimestampUTC())});
    enriched.push_back({"time_synced", FieldValue::makeBool(isTimeSynced())});
    enriched.push_back({"mono_ms", FieldValue::makeInt(getMonoMs())});
    enriched.push_back({"level", FieldValue::makeString(logLevelToString(level))});
    enriched.push_back({"service", FieldValue::makeString(m_service)});
    enriched.push_back({"module", FieldValue::makeString(module)});
    enriched.push_back({"event", FieldValue::makeString(event)});
    enriched.push_back({"message", FieldValue::makeString(message)});
    enriched.push_back({"pid", FieldValue::makeInt(static_cast<int64_t>(m_pid))});
    enriched.push_back({"tid", FieldValue::makeInt(static_cast<int64_t>(current_tid()))});

    if (context) {
        if (!context->trace_id.empty()) {
            enriched.push_back({"trace_id", FieldValue::makeString(context->trace_id)});
        }
        if (!context->request_id.empty()) {
            enriched.push_back({"request_id", FieldValue::makeString(context->request_id)});
        }
        if (!context->session_id.empty()) {
            enriched.push_back({"session_id", FieldValue::makeString(context->session_id)});
        }
    }

    enriched.insert(enriched.end(), fields.begin(), fields.end());
    return enriched;
}

int64_t Enricher::getMonoMs() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - m_startTime).count();
}

std::string Enricher::getTimestampUTC() const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct tm tm_result;
    time_t sec = tv.tv_sec;
    gmtime_r(&sec, &tm_result);

    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             tm_result.tm_year + 1900, tm_result.tm_mon + 1, tm_result.tm_mday,
             tm_result.tm_hour, tm_result.tm_min, tm_result.tm_sec,
             static_cast<int>(tv.tv_usec / 1000));
    return std::string(buf);
}

bool Enricher::isTimeSynced() const {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec > 1577836800L; // 2020-01-01
}

} // namespace log
} // namespace fw
} // namespace ccu
