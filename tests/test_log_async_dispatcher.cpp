#include "log_types.h"
#include "log/log_async_dispatcher.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <atomic>

using namespace ccu::fw::log;

void test_async_basic_submit() {
    std::vector<std::string> written;
    auto writer = [&written](const std::string& line, LogLevel level) -> bool {
        written.push_back(line);
        return true;
    };

    AsyncDispatcher dispatcher(100, 100, writer);
    dispatcher.start();

    assert(dispatcher.submit("{\"msg\":\"test1\"}", LogLevel::kInfo) == true);
    assert(dispatcher.submit("{\"msg\":\"test2\"}", LogLevel::kWarn) == true);

    dispatcher.flush();
    dispatcher.stop();

    assert(written.size() == 2);
    std::cout << "  [PASS] test_async_basic_submit" << std::endl;
}

void test_async_queue_overflow_low_level() {
    std::atomic<int> writeCount{0};
    auto writer = [&writeCount](const std::string&, LogLevel) -> bool {
        ++writeCount;
        return true;
    };

    AsyncDispatcher dispatcher(2, 50, writer);
    dispatcher.start();

    assert(dispatcher.submit("1", LogLevel::kInfo) == true);
    assert(dispatcher.submit("2", LogLevel::kInfo) == true);

    assert(dispatcher.submit("3", LogLevel::kDebug) == false);
    assert(dispatcher.getDroppedCount() == 1);

    dispatcher.stop();
    std::cout << "  [PASS] test_async_queue_overflow_low_level" << std::endl;
}

void test_async_queue_overflow_high_level_sync() {
    std::vector<std::string> written;
    auto writer = [&written](const std::string& line, LogLevel level) -> bool {
        written.push_back(line);
        return true;
    };

    AsyncDispatcher dispatcher(2, 50, writer);
    dispatcher.start();

    dispatcher.submit("1", LogLevel::kInfo);
    dispatcher.submit("2", LogLevel::kInfo);

    assert(dispatcher.submit("error_msg", LogLevel::kError) == true);

    dispatcher.flush();
    dispatcher.stop();

    bool found = false;
    for (const auto& w : written) {
        if (w.find("error_msg") != std::string::npos) {
            found = true;
            break;
        }
    }
    assert(found);

    std::cout << "  [PASS] test_async_queue_overflow_high_level_sync" << std::endl;
}

void test_async_dropped_count() {
    auto writer = [](const std::string&, LogLevel) -> bool { return true; };
    AsyncDispatcher dispatcher(1, 50, writer);
    dispatcher.start();

    dispatcher.submit("1", LogLevel::kInfo);
    dispatcher.submit("2", LogLevel::kInfo);
    dispatcher.submit("3", LogLevel::kDebug);

    assert(dispatcher.getDroppedCount() == 2);

    dispatcher.stop();
    std::cout << "  [PASS] test_async_dropped_count" << std::endl;
}

int main() {
    std::cout << "Running AsyncDispatcher tests..." << std::endl;
    test_async_basic_submit();
    test_async_queue_overflow_low_level();
    test_async_queue_overflow_high_level_sync();
    test_async_dropped_count();
    std::cout << "All AsyncDispatcher tests passed!" << std::endl;
    return 0;
}
