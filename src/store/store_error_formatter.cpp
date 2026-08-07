#include "store_error_formatter.h"

namespace cgw {
namespace fw {
namespace store {

std::string StoreErrorFormatter::format(std::string_view code,
                                        std::string_view reason,
                                        std::string_view service,
                                        std::string_view key,
                                        std::string_view path,
                                        std::string_view stage) {
    std::string msg(code);
    msg += ": ";
    msg += reason;
    bool first = true;
    auto appendField = [&](const char* name, std::string_view val) {
        if (val.empty()) return;
        msg += (first ? " [" : ", ");
        first = false;
        msg += name;
        msg += "=";
        msg += val;
    };
    appendField("service", service);
    appendField("key", key);
    appendField("path", path);
    appendField("stage", stage);
    if (!first) msg += "]";
    return msg;
}

} // namespace store
} // namespace fw
} // namespace cgw
