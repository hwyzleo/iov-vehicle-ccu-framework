#include "log_emergency_writer.h"
#include <unistd.h>
#include <cstring>

namespace cgw {
namespace fw {
namespace log {

void EmergencyWriter::write(const char* msg) {
    if (!msg) return;
    size_t len = strlen(msg);
    ssize_t ret = ::write(STDERR_FILENO, msg, len);
    (void)ret;
}

void EmergencyWriter::write(const std::string& msg) {
    write(msg.c_str());
}

} // namespace log
} // namespace fw
} // namespace cgw
