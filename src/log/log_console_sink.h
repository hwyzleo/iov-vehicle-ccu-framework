#pragma once

#include <string>

namespace cgw {
namespace fw {
namespace log {

class ConsoleSink {
public:
    ConsoleSink();
    ~ConsoleSink();

    bool write(const std::string& line, bool isError = false);
    void flush();
    bool isAvailable() const;

private:
    bool m_available = true;
};

} // namespace log
} // namespace fw
} // namespace cgw
