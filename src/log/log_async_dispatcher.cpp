#include "log_async_dispatcher.h"
#include <chrono>

namespace cgw {
namespace fw {
namespace log {

AsyncDispatcher::AsyncDispatcher(uint32_t queueSize, uint32_t flushIntervalMs, Writer writer)
    : m_queueSize(queueSize)
    , m_flushIntervalMs(flushIntervalMs)
    , m_writer(std::move(writer))
{
}

AsyncDispatcher::~AsyncDispatcher() {
    stop();
}

void AsyncDispatcher::start() {
    if (m_running.exchange(true)) return;
    m_worker = std::thread(&AsyncDispatcher::workerLoop, this);
}

void AsyncDispatcher::stop() {
    if (!m_running.exchange(false)) return;
    m_cond.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!m_queue.empty()) {
        auto& item = m_queue.front();
        m_writer(item.first, item.second);
        m_queue.pop();
    }
}

bool AsyncDispatcher::submit(const std::string& line, LogLevel level) {
    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_queue.size() < m_queueSize) {
        m_queue.emplace(line, level);
        m_cond.notify_one();
        return true;
    }

    if (isHighPriority(level)) {
        lock.unlock();
        m_writer(line, level);
        return true;
    }

    if (level == LogLevel::kWarn) {
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        lock.lock();
        if (m_queue.size() < m_queueSize) {
            m_queue.emplace(line, level);
            m_cond.notify_one();
            return true;
        }
    }

    m_droppedCount.fetch_add(1);
    return false;
}

void AsyncDispatcher::flush() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_flushCond.wait_for(lock, std::chrono::milliseconds(m_flushIntervalMs * 2), [this]() {
        return m_queue.empty();
    });
}

uint64_t AsyncDispatcher::getDroppedCount() const {
    return m_droppedCount.load();
}

void AsyncDispatcher::workerLoop() {
    // 临时缓冲区，减少持锁时间
    using QueueItem = std::pair<std::string, LogLevel>;

    while (m_running) {
        std::vector<QueueItem> batch;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cond.wait_for(lock, std::chrono::milliseconds(m_flushIntervalMs), [this]() {
                return !m_queue.empty() || !m_running;
            });

            while (!m_queue.empty() && batch.size() < 64) {
                batch.push_back(std::move(m_queue.front()));
                m_queue.pop();
            }
        }

        for (const auto& item : batch) {
            m_writer(item.first, item.second);
        }

        if (batch.size() > 0) {
            m_flushCond.notify_all();
        }
    }
}

bool AsyncDispatcher::isHighPriority(LogLevel level) const {
    return level == LogLevel::kError || level == LogLevel::kFatal;
}

} // namespace log
} // namespace fw
} // namespace cgw
