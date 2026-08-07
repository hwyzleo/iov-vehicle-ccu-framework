// cgw-framework-hash - FIPS 180-4 SHA-256 one-shot digest.
// CGW-FW-DSN-CR-004: business-agnostic, binary-safe, cross-platform
// deterministic SHA-256. Independent in-process C++ static library.
//
// Boundary (CR-004 §1/§7/§8):
//   - Does NOT read config/files/network, holds no key material, and does not
//     depend on config/store/log/SOME-IP. Other components MAY consume hash.
//   - This is a keyless content fingerprint ONLY. It is NOT a signature, MAC,
//     source-authentication or active anti-tamper credential.
//
// Implementation: self-contained minimal FIPS 180-4 SHA-256, no third-party
// runtime dependency (CR-004 §2/§11). License: project LICENSE. The Core lives
// as a private Sha256Context in src/sha256.cpp and is NOT part of the public
// API (CR-004 §5). No local modifications beyond CGW namespace adaptation of
// the public-domain algorithm.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cgw::fw::hash {

// Binary-safe byte view. Does not rely on a C-string terminator.
// ByteView{nullptr, 0} is a valid empty input; size > 0 && data == nullptr is
// illegal and yields HashException(CGW-FW-0401).
struct ByteView {
    const std::uint8_t* data{nullptr};
    std::size_t size{0};
};

// SHA-256 raw digest, fixed 32 bytes; byte order matches the standard display
// order (big-endian).
using Sha256Digest = std::array<std::uint8_t, 32>;

// Exception carrying CGW-FW-04xx error codes (CR-004 §7).
//   CGW-FW-0401: non-zero length null pointer, bit-length overflow or invalid
//                input range - rejected before computation, no partial digest.
//   CGW-FW-0402: internal invariant / self-check / backend failure - aborts
//                the computation, no partial digest.
// Normal computation produces no log output; failure diagnostics MUST NOT
// record the raw input (only module, input length, error code and stage).
struct HashException : std::runtime_error {
    std::string code;  // "CGW-FW-0401" / "CGW-FW-0402"

    HashException(std::string code_, const std::string& message)
        : std::runtime_error(message), code(std::move(code_)) {}
};

// One-shot SHA-256 raw digest (32 bytes).
// Throws HashException(CGW-FW-0401) for a non-zero-length null pointer or
// bit-length overflow; HashException(CGW-FW-0402) on internal failure.
Sha256Digest sha256(ByteView input);

// One-shot SHA-256 as 64 lowercase hex chars [0-9a-f]; no 0x prefix,
// separator, whitespace or newline. Semantically equivalent to a fixed
// lowercase hex encoding of sha256(input).
std::string sha256_hex(ByteView input);

// std::string_view convenience overloads: processed by full byte length,
// internal NUL is not truncated and no charset conversion is performed.
inline Sha256Digest sha256(std::string_view input) {
    return sha256(ByteView{reinterpret_cast<const std::uint8_t*>(input.data()),
                           input.size()});
}

inline std::string sha256_hex(std::string_view input) {
    return sha256_hex(ByteView{reinterpret_cast<const std::uint8_t*>(input.data()),
                               input.size()});
}

}  // namespace cgw::fw::hash
