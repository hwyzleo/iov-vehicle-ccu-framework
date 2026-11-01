#pragma once

#include <string>

namespace ccu {
namespace fw {
namespace log {

// 最小化、无递归的紧急输出（异步信号安全）
class EmergencyWriter {
public:
    static void write(const char* msg);
    static void write(const std::string& msg);
};

} // namespace log
} // namespace fw
} // namespace ccu
