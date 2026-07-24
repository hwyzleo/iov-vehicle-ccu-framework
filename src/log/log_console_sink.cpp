#include "log_console_sink.h"
#include <cstdio>

namespace cgw {
namespace fw {
namespace log {

ConsoleSink::ConsoleSink() : m_available(true) {}

ConsoleSink::~ConsoleSink() {
    flush();
}

bool ConsoleSink::write(const std::string& line, bool isError) {
    if (!m_available) return false;

    FILE* fp = isError ? stderr : stdout;
    std::string output = line + "\n";

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
