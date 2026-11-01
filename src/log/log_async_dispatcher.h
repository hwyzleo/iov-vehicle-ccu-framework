#pragma once

#include "log_types.h"
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <cstdint>
#include <utility>

namespace ccu {
namespace fw {
namespace log {

class AsyncDispatcher {
public:
    using Writer = std::function<bool(const std::string& line, LogLevel level)>;

    AsyncDispatcher(uint32_t queueSize, uint32_t flushIntervalMs, Writer writer);
    ~AsyncDispatcher();

    bool submit(const std::string& line, LogLevel level);
    void flush();
    uint64_t getDroppedCount() const;
    void start();
    void stop();

private:
    uint32_t m_queueSize;
    uint32_t m_flushIntervalMs;
    Writer m_writer;

    // 每个条目包含格式化后的日志行和对应的级别
    std::queue<std::pair<std::string, LogLevel>> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
    std::condition_variable m_flushCond;

    std::thread m_worker;
    std::atomic<bool> m_running{false};
    std::atomic<uint64_t> m_droppedCount{0};

    void workerLoop();
    bool isHighPriority(LogLevel level) const;
};

} // namespace log
} // namespace fw
} // namespace ccu
