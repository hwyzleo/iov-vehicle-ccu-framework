#include "config_error_formatter.h"

namespace cgw {
namespace fw {
namespace config {

std::string ConfigErrorFormatter::format(std::string_view code,
                                         std::string_view reason,
                                         std::string_view path,
                                         std::string_view source,
                                         std::string_view key) {
    std::string msg;
    msg.append(code);
    msg.append(": ");
    msg.append(reason);

    std::string meta;
    auto appendField = [&](const char* name, std::string_view val) {
        if (val.empty()) return;
        if (!meta.empty()) meta.push_back(' ');
        meta.append(name);
        meta.append("=");
        meta.append(val);
    };
    appendField("path", path);
    appendField("source", source);
    appendField("key", key);
    if (!meta.empty()) {
        msg.append(" [");
        msg.append(meta);
        msg.append("]");
    }
    return msg;
}

} // namespace config
} // namespace fw
} // namespace cgw
