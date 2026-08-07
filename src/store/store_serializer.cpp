// Implements the versioned binary framing declared in store_types.h (detail).
//
// Layout:
//   header (8 bytes): magic 'C','G','W','S' | version u8 | typeTag u16 LE | reserved u8
//   payload (type specific, little-endian):
//     String    : u32 LE length + UTF-8 bytes
//     Int64     : 8 bytes LE
//     Double    : 8 bytes IEEE754 LE
//     Bool      : 1 byte (0/1)
//     StringVec : u32 LE count + (u32 len + bytes)*
//     Int64Vec  : u32 LE count + 8 bytes*count
// load<T> verifies magic/version/typeTag; mismatch -> CGW-FW-0104.

#include "store_types.h"
#include "store_error_formatter.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace cgw {
namespace fw {
namespace store {
namespace detail {

namespace {

constexpr char kMagic[4] = {'C', 'G', 'W', 'S'};
constexpr uint8_t kFormatVersion = 1;

enum class TypeTag : uint16_t {
    String    = 1,
    Int64     = 2,
    Double    = 3,
    Bool      = 4,
    StringVec = 5,
    Int64Vec  = 6,
};

void fail104(const std::string& reason) {
    throw StoreException(
        "CGW-FW-0104",
        StoreErrorFormatter::format("CGW-FW-0104", reason, "", "", "", "decode"),
        "", "", "", "decode");
}

void putU16LE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}
void putU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}
void putU64LE(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
}

void writeHeader(std::vector<uint8_t>& out, TypeTag tag) {
    out.push_back(static_cast<uint8_t>(kMagic[0]));
    out.push_back(static_cast<uint8_t>(kMagic[1]));
    out.push_back(static_cast<uint8_t>(kMagic[2]));
    out.push_back(static_cast<uint8_t>(kMagic[3]));
    out.push_back(kFormatVersion);
    putU16LE(out, static_cast<uint16_t>(tag));
    out.push_back(0); // reserved
}

uint16_t readU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
uint32_t readU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t readU64LE(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(p[i]) << (8 * i);
    return v;
}

// Verify header magic/version and that the stored tag matches `expected`.
// Returns pointer to payload (after the 8-byte header).
const uint8_t* checkHeader(const std::vector<uint8_t>& b, TypeTag expected) {
    if (b.size() < 8) fail104("truncated header");
    if (b[0] != kMagic[0] || b[1] != kMagic[1] || b[2] != kMagic[2] ||
        b[3] != kMagic[3]) {
        fail104("bad magic");
    }
    if (b[4] != kFormatVersion) fail104("unsupported format version");
    if (readU16LE(&b[5]) != static_cast<uint16_t>(expected)) {
        fail104("type tag mismatch");
    }
    return b.data() + 8;
}

} // namespace

// ---- encode ----
std::vector<uint8_t> encode(const std::string& v) {
    std::vector<uint8_t> out;
    writeHeader(out, TypeTag::String);
    putU32LE(out, static_cast<uint32_t>(v.size()));
    out.insert(out.end(), v.begin(), v.end());
    return out;
}
std::vector<uint8_t> encode(int64_t v) {
    std::vector<uint8_t> out;
    writeHeader(out, TypeTag::Int64);
    putU64LE(out, static_cast<uint64_t>(v));
    return out;
}
std::vector<uint8_t> encode(double v) {
    std::vector<uint8_t> out;
    writeHeader(out, TypeTag::Double);
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    putU64LE(out, bits);
    return out;
}
std::vector<uint8_t> encode(bool v) {
    std::vector<uint8_t> out;
    writeHeader(out, TypeTag::Bool);
    out.push_back(v ? 1u : 0u);
    return out;
}
std::vector<uint8_t> encode(const std::vector<std::string>& v) {
    std::vector<uint8_t> out;
    writeHeader(out, TypeTag::StringVec);
    putU32LE(out, static_cast<uint32_t>(v.size()));
    for (const auto& s : v) {
        putU32LE(out, static_cast<uint32_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
    return out;
}
std::vector<uint8_t> encode(const std::vector<int64_t>& v) {
    std::vector<uint8_t> out;
    writeHeader(out, TypeTag::Int64Vec);
    putU32LE(out, static_cast<uint32_t>(v.size()));
    for (int64_t x : v) putU64LE(out, static_cast<uint64_t>(x));
    return out;
}

// ---- decode ----
std::string decodeString(const std::vector<uint8_t>& b) {
    const uint8_t* p = checkHeader(b, TypeTag::String);
    if (b.size() < 8 + 4) fail104("truncated string length");
    uint32_t len = readU32LE(p);
    if (b.size() != 8 + 4 + len) fail104("string length mismatch");
    return std::string(reinterpret_cast<const char*>(p + 4), len);
}
int64_t decodeInt64(const std::vector<uint8_t>& b) {
    const uint8_t* p = checkHeader(b, TypeTag::Int64);
    if (b.size() != 8 + 8) fail104("int64 size mismatch");
    return static_cast<int64_t>(readU64LE(p));
}
double decodeDouble(const std::vector<uint8_t>& b) {
    const uint8_t* p = checkHeader(b, TypeTag::Double);
    if (b.size() != 8 + 8) fail104("double size mismatch");
    uint64_t bits = readU64LE(p);
    double v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}
bool decodeBool(const std::vector<uint8_t>& b) {
    const uint8_t* p = checkHeader(b, TypeTag::Bool);
    if (b.size() != 8 + 1) fail104("bool size mismatch");
    return p[0] != 0;
}
std::vector<std::string> decodeStringVec(const std::vector<uint8_t>& b) {
    const uint8_t* p = checkHeader(b, TypeTag::StringVec);
    const uint8_t* end = b.data() + b.size();
    if (end - p < 4) fail104("truncated string vector count");
    uint32_t count = readU32LE(p);
    p += 4;
    std::vector<std::string> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (end - p < 4) fail104("truncated string vector element");
        uint32_t len = readU32LE(p);
        p += 4;
        if (end - p < static_cast<ptrdiff_t>(len)) fail104("string vector element overflow");
        out.emplace_back(reinterpret_cast<const char*>(p), len);
        p += len;
    }
    if (p != end) fail104("trailing bytes in string vector");
    return out;
}
std::vector<int64_t> decodeInt64Vec(const std::vector<uint8_t>& b) {
    const uint8_t* p = checkHeader(b, TypeTag::Int64Vec);
    const uint8_t* end = b.data() + b.size();
    if (end - p < 4) fail104("truncated int64 vector count");
    uint32_t count = readU32LE(p);
    p += 4;
    if (end - p != static_cast<ptrdiff_t>(count) * 8) fail104("int64 vector size mismatch");
    std::vector<int64_t> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        out.push_back(static_cast<int64_t>(readU64LE(p)));
        p += 8;
    }
    return out;
}

} // namespace detail
} // namespace store
} // namespace fw
} // namespace cgw
