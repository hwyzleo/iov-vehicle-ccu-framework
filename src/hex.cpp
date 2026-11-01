// src/hex.cpp - reversible byte<->Hex codec for ccu-framework-hash.
// CCU-FW-DSN-CR-007.
//
// Self-contained, C++17 standard library only. No third-party runtime
// dependency. License: project LICENSE.
//
// Implementation constraints (CR-007 §5/§6):
//   - Bytes are treated explicitly as std::uint8_t; no signed-char dependency.
//   - Fixed lowercase table; no locale, std::hex, stringstream or per-char
//     append growth. One allocation of the final output length.
//   - Decoding accepts [0-9a-fA-F]; no std::isxdigit/std::tolower (locale and
//     negative-char UB). Parity is checked before allocation/decode.
//   - No global mutable state; per-call locals only; reentrant/thread-safe.
//   - On error, no partial result is exposed: the result is a function-local
//     return object and the exception propagates before it is returned.

#include "ccu/fw/hash/hex.hpp"

namespace ccu::fw::hash {
namespace {

// Fixed lowercase Hex table (CR-007 §5). Internal to this translation unit;
// sha256.cpp has its own anonymous-namespace table for the digest Hex path.
constexpr char kHex[] = "0123456789abcdef";

// Decode a single Hex nibble. Returns 0..15 for [0-9a-fA-F], or -1 otherwise.
// No locale / std::isxdigit / std::tolower (CR-007 §6).
inline int decodeNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

HexException::HexException(std::string code, std::string message,
                           std::size_t position)
    : std::runtime_error(message),
      code_(std::move(code)),
      position_(position) {}

const std::string& HexException::code() const noexcept { return code_; }

std::size_t HexException::position() const noexcept { return position_; }

std::string bytesToHex(ByteView input) {
    // CR-007 §5: validate pointer and output-length range before encoding.
    if (input.size > 0 && input.data == nullptr) {
        throw HexException(
            "CCU-FW-0412",
            "bytesToHex: non-zero input length with null data pointer");
    }
    // Output length is 2 * input.size; it must be representable in a string.
    if (input.size > std::string{}.max_size() / 2) {
        throw HexException(
            "CCU-FW-0412",
            "bytesToHex: output length overflows string representation");
    }

    std::string out;
    out.resize(input.size * 2);
    for (std::size_t i = 0; i < input.size; ++i) {
        const std::uint8_t b = input.data[i];
        out[i * 2] = kHex[(b >> 4) & 0x0f];
        out[i * 2 + 1] = kHex[b & 0x0f];
    }
    return out;
}

std::string bytesToHex(std::string_view input) {
    return bytesToHex(ByteView{
        reinterpret_cast<const std::uint8_t*>(input.data()), input.size()});
}

std::string hexToBytes(std::string_view hex) {
    // CR-007 §6: check parity before allocation/decode; no partial result.
    if (hex.size() % 2 != 0) {
        throw HexException(
            "CCU-FW-0410",
            "hexToBytes: odd Hex length");
    }

    std::string out;
    out.resize(hex.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int hi = decodeNibble(hex[i * 2]);
        if (hi < 0) {
            throw HexException(
                "CCU-FW-0411",
                "hexToBytes: invalid Hex character",
                i * 2);
        }
        const int lo = decodeNibble(hex[i * 2 + 1]);
        if (lo < 0) {
            throw HexException(
                "CCU-FW-0411",
                "hexToBytes: invalid Hex character",
                i * 2 + 1);
        }
        out[i] = static_cast<char>((static_cast<std::uint8_t>(hi) << 4) |
                                   static_cast<std::uint8_t>(lo));
    }
    return out;
}

}  // namespace ccu::fw::hash
