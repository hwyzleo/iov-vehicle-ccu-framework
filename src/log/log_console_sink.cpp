#include "log_console_sink.h"
#include "log_types.h"  // logLevelToString
#include <cstdio>
#include <cstring>
#include <cctype>
#include <unistd.h>

namespace cgw {
namespace fw {
namespace log {

ConsoleSink::ConsoleSink() : m_available(true) {}

ConsoleSink::~ConsoleSink() {
    flush();
}

const char* ConsoleSink::ansiColorForLevel(LogLevel level) {
    // 仅终端时才返回颜色码
    // 注意：首次调用时检测，后续可使用缓存避免重复 isatty 系统调用
    // 但考虑到 stdout/stderr 可能被运行时重定向，每次都检测更安全
    switch (level) {
        case LogLevel::kTrace:
        case LogLevel::kDebug:
            return "\033[2m";   // dim（灰色）
        case LogLevel::kInfo:
            return "\033[32m";  // green
        case LogLevel::kWarn:
            return "\033[33m";  // yellow
        case LogLevel::kError:
        case LogLevel::kFatal:
            return "\033[31m";  // red
        default:
            return nullptr;
    }
}

bool ConsoleSink::write(const std::string& line, LogLevel level) {
    if (!m_available) return false;

    bool isError = (level >= LogLevel::kError);
    FILE* fp = isError ? stderr : stdout;
    int fd = fileno(fp);

    // 仅在终端输出时添加颜色，且只对 [level] 标签着色
    bool useColor = isatty(fd);
    const char* color = useColor ? ansiColorForLevel(level) : nullptr;

    std::string output;
    if (color) {
        // 格式: [timestamp] [service] [level] message
        // 只对第三个方括号对（即 [level]）着色
        const char* reset = "\033[0m";
        const char* levelTag = logLevelToString(level);

        // 构建要搜索的标记，例如 "[info]"
        std::string marker;
        marker.reserve(std::strlen(levelTag) + 2);
        marker.push_back('[');
        // 日志行中 level 是小写的
        for (const char* p = levelTag; *p; ++p) {
            marker.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
        }
        marker.push_back(']');

        size_t pos = line.find(marker);
        if (pos != std::string::npos) {
            output.reserve(line.size() + std::strlen(color) + std::strlen(reset) + 1);
            output.append(line, 0, pos);        // marker 之前的部分
            output.append(color);                // 颜色码
            output.append(line, pos, marker.size()); // [level] 标签
            output.append(reset);                // 重置码
            output.append(line, pos + marker.size()); // 标签之后的部分
        } else {
            // 未找到标记，原样输出（不应发生）
            output = line;
        }
        output.push_back('\n');
    } else {
        output = line + "\n";
    }

    size_t written = fwrite(output.c_str(), 1, output.size(), fp);
    if (written != output.size()) {
        m_available = false;
        return false;
    }
    return true;
}

void ConsoleSink::flush() {
    fflush(stdout);
    fflush(stderr);
}

bool ConsoleSink::isAvailable() const {
    return m_available;
}

} // namespace log
} // namespace fw
} // namespace cgw
