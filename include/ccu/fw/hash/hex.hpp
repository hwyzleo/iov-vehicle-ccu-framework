// ccu-framework-hash - reversible binary-safe byte<->Hex codec.
// CCU-FW-DSN-CR-007: lowercase Hex encoding, case-insensitive decoding, no
// digest/signature/encryption. Shares the ccu-framework-hash target and
// reuses ByteView from sha256.hpp; sha256()/sha256_hex() are unchanged.
//
// Boundary (CR-007 §2/§7):
//   - Does NOT compute a digest; bytesToHex/hexToBytes are a 1:1 reversible
//     mapping between raw bytes and ASCII Hex only.
//   - Does NOT read config/files/network, holds no key material, and does not
//     depend on config/store/log/SOME-IP. Other components MAY consume it.
//   - Hex encoding is NOT a security protection, integrity check or source
//     authentication.
//
// Implementation: self-contained, C++17 standard library only (CR-007 §1/§5).
// License: project LICENSE. No third-party runtime dependency.
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

#include <ccu/fw/hash/sha256.hpp>  // ByteView

namespace ccu::fw::hash {

// Exception carrying CCU-FW-041x error codes (CR-007 §7).
//   CCU-FW-0410: Hex input length is odd - rejected before allocation/decode.
//   CCU-FW-0411: Hex input contains a non-[0-9a-fA-F] character - carries the
//                offending character index in position(); no partial result.
//   CCU-FW-0412: non-zero-length null pointer or unrepresentable output
//                length - rejected before encoding.
// position() is npos except for CCU-FW-0411, where it is the invalid char
// index. Failure diagnostics MUST NOT record the full input, raw bytes or any
// decoded prefix (only module, input length, error code, stage and index).
class HexException : public std::runtime_error {
public:
    HexException(std::string code, std::string message,
                 std::size_t position = std::string_view::npos);

    const std::string& code() const noexcept;
    std::size_t position() const noexcept;

private:
    std::string code_;
    std::size_t position_;
};

// Encode raw bytes to lowercase Hex [0-9a-f], two chars per byte, high nibble
// first. No 0x prefix, separator, whitespace or newline. Empty input returns
// an empty string. ByteView{nullptr, 0} is a legal empty input; a non-zero
// length with a null pointer or an output length that cannot be represented
// throws HexException(CCU-FW-0412).
std::string bytesToHex(ByteView input);

// std::string_view convenience overload: adapts the byte address and length to
// a ByteView. Internal NUL is processed as an ordinary byte; no charset
// conversion is performed.
std::string bytesToHex(std::string_view input);

// Decode lowercase/uppercase/mixed-case Hex to raw bytes. Every two Hex chars
// restore one byte; hexToBytes(bytesToHex(x)) == x. Empty input returns an
// empty string. Odd length throws HexException(CCU-FW-0410); a non-Hex
// character throws HexException(CCU-FW-0411) with the character index. The
// result is returned by value; on error no partial result is exposed.
// Returns std::string so it can carry arbitrary bytes (including internal NUL)
// for consumers such as FOTA protobuf binary.
std::string hexToBytes(std::string_view hex);

}  // namespace ccu::fw::hash
