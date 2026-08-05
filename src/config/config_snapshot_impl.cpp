#include "config_snapshot_impl.h"
#include "config_error_formatter.h"
#include <algorithm>
#include <stdexcept>
#include <cctype>

namespace cgw {
namespace fw {
namespace config {
namespace detail {

namespace {

std::vector<std::string> splitDotted(std::string_view key) {
    std::vector<std::string> segs;
    size_t start = 0;
    while (start <= key.size()) {
        size_t pos = key.find('.', start);
        if (pos == std::string_view::npos) {
            segs.emplace_back(key.substr(start));
            break;
        }
        segs.emplace_back(key.substr(start, pos - start));
        start = pos + 1;
    }
    return segs;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool parseBool(const std::string& s, bool& out) {
    std::string l = toLower(s);
    if (l == "true" || l == "yes" || l == "on" || l == "1") { out = true; return true; }
    if (l == "false" || l == "no" || l == "off" || l == "0") { out = false; return true; }
    return false;
}

} // namespace

void SnapshotImpl::throwMissing(std::string_view key) {
    throw ConfigException(
        "CGW-FW-0003",
        ConfigErrorFormatter::format(
            "CGW-FW-0003", "missing required config key", "", "", std::string(key)),
        "", std::string(key));
}

void SnapshotImpl::throwType(std::string_view key, const std::string& detail) {
    throw ConfigException(
        "CGW-FW-0004",
        ConfigErrorFormatter::format(
            "CGW-FW-0004", "config value type/range invalid: " + detail, "", "", std::string(key)),
        "", std::string(key));
}

const ConfigNode* SnapshotImpl::lookup(std::string_view key) const {
    if (!m_root || m_root->type != NodeType::kMap) return nullptr;
    auto segs = splitDotted(key);
    const ConfigNode* cur = m_root.get();
    for (const auto& seg : segs) {
        if (!cur || cur->type != NodeType::kMap) return nullptr;
        cur = cur->find(seg);
        if (!cur) return nullptr;
    }
    return cur;
}

const ConfigNode* SnapshotImpl::lookupSection(std::string_view section) const {
    if (section.empty()) return m_root.get();
    return lookup(section);
}

bool SnapshotImpl::has(std::string_view key) const {
    return lookup(key) != nullptr;
}

std::vector<std::string> SnapshotImpl::keys(std::string_view section) const {
    const ConfigNode* node = lookupSection(section);
    if (!node || node->type != NodeType::kMap) return {};
    std::vector<std::string> out;
    out.reserve(node->map.size());
    for (const auto& kv : node->map) out.push_back(kv.first);
    return out;
}

std::string SnapshotImpl::getString(std::string_view key) const {
    const ConfigNode* n = lookup(key);
    if (!n) throwMissing(key);
    if (n->type != NodeType::kScalar) throwType(key, "not a scalar");
    return n->scalar;
}

int64_t SnapshotImpl::getInt64(std::string_view key) const {
    const ConfigNode* n = lookup(key);
    if (!n) throwMissing(key);
    if (n->type != NodeType::kScalar) throwType(key, "not a scalar");
    try {
        size_t pos = 0;
        int64_t v = std::stoll(n->scalar, &pos, 0); // base 0: auto-detect 0x/0/10
        if (pos != n->scalar.size()) throwType(key, "integer has trailing characters");
        return v;
    } catch (const ConfigException&) {
        throw;
    } catch (const std::exception&) {
        throwType(key, "cannot parse as integer");
    }
    return 0; // unreachable
}

double SnapshotImpl::getDouble(std::string_view key) const {
    const ConfigNode* n = lookup(key);
    if (!n) throwMissing(key);
    if (n->type != NodeType::kScalar) throwType(key, "not a scalar");
    try {
        size_t pos = 0;
        double v = std::stod(n->scalar, &pos);
        if (pos != n->scalar.size()) throwType(key, "double has trailing characters");
        return v;
    } catch (const ConfigException&) {
        throw;
    } catch (const std::exception&) {
        throwType(key, "cannot parse as double");
    }
    return 0.0; // unreachable
}

bool SnapshotImpl::getBool(std::string_view key) const {
    const ConfigNode* n = lookup(key);
    if (!n) throwMissing(key);
    if (n->type != NodeType::kScalar) throwType(key, "not a scalar");
    bool out = false;
    if (!parseBool(n->scalar, out)) throwType(key, "cannot parse as boolean");
    return out;
}

std::vector<std::string> SnapshotImpl::getStringVec(std::string_view key) const {
    const ConfigNode* n = lookup(key);
    if (!n) throwMissing(key);
    if (n->type != NodeType::kSequence) throwType(key, "not a sequence");
    std::vector<std::string> out;
    out.reserve(n->sequence.size());
    for (const auto& e : n->sequence) {
        if (e->type != NodeType::kScalar) throwType(key, "sequence element not a scalar");
        out.push_back(e->scalar);
    }
    return out;
}

std::vector<int64_t> SnapshotImpl::getInt64Vec(std::string_view key) const {
    const ConfigNode* n = lookup(key);
    if (!n) throwMissing(key);
    if (n->type != NodeType::kSequence) throwType(key, "not a sequence");
    std::vector<int64_t> out;
    out.reserve(n->sequence.size());
    for (const auto& e : n->sequence) {
        if (e->type != NodeType::kScalar) throwType(key, "sequence element not a scalar");
        try {
            size_t pos = 0;
            int64_t v = std::stoll(e->scalar, &pos, 0);
            if (pos != e->scalar.size()) throwType(key, "integer element has trailing characters");
            out.push_back(v);
        } catch (const ConfigException&) {
            throw;
        } catch (const std::exception&) {
            throwType(key, "cannot parse sequence element as integer");
        }
    }
    return out;
}

// ============================================================
// Bridge free functions (declared in config_types.h, used by templates)
// ============================================================
std::string snapshotGetString(const SnapshotImpl& i, std::string_view k) { return i.getString(k); }
int64_t     snapshotGetInt64(const SnapshotImpl& i, std::string_view k) { return i.getInt64(k); }
double      snapshotGetDouble(const SnapshotImpl& i, std::string_view k) { return i.getDouble(k); }
bool        snapshotGetBool(const SnapshotImpl& i, std::string_view k) { return i.getBool(k); }
std::vector<std::string> snapshotGetStringVec(const SnapshotImpl& i, std::string_view k) { return i.getStringVec(k); }
std::vector<int64_t>     snapshotGetInt64Vec(const SnapshotImpl& i, std::string_view k) { return i.getInt64Vec(k); }
bool        snapshotHas(const SnapshotImpl& i, std::string_view k) { return i.has(k); }
std::vector<std::string> snapshotKeys(const SnapshotImpl& i, std::string_view s) { return i.keys(s); }

} // namespace detail
} // namespace config
} // namespace fw
} // namespace cgw
