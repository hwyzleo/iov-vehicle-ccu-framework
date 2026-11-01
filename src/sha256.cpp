// src/sha256.cpp - FIPS 180-4 SHA-256 Core + Hex encoder + facade.
// CCU-FW-DSN-CR-004.
//
// Self-contained minimal implementation. No third-party runtime dependency.
// License: project LICENSE. FIPS 180-4 mapping: §5 (message padding, message
// schedule, compression). Local modifications: none beyond CCU namespace
// adaptation of the public-domain algorithm.
//
// Implementation constraints (CR-004 §5):
//   - std::uint32_t unsigned modular arithmetic only; no signed overflow.
//   - Big-endian read/write via explicit shifts; no unaligned pointer casts,
//     no host-endian dependency.
//   - bit length as std::uint64_t; reject overflow before computation (0401).
//   - Fixed state + a single 64-byte block; streams input without copying the
//     full buffer.
//   - No global mutable state; per-call local context; reentrant/thread-safe.

#include "ccu/fw/hash/sha256.hpp"

#include <cstring>

namespace ccu::fw::hash {
namespace {

// FIPS 180-4 §4.2.2 initial hash values H0..H7.
constexpr std::uint32_t kInitState[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};

// FIPS 180-4 §4.2.2 round constants K0..K63.
constexpr std::uint32_t kRoundK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
    0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
    0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
    0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
    0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
    0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
    0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
    0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

// Total bits must fit in std::uint64_t: input bytes * 8 must not overflow.
// bytes <= UINT64_MAX / 8. Anything larger is rejected as CCU-FW-0401 before
// any computation (and before any input byte is read).
constexpr std::uint64_t kMaxInputBytes = UINT64_MAX / 8u;

inline std::uint32_t rotr(std::uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

// Big-endian load/store via explicit shifts (no unaligned pointer casts).
inline std::uint32_t loadBe32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

inline void storeBe32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

inline void storeBe64(std::uint8_t* p, std::uint64_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 56);
    p[1] = static_cast<std::uint8_t>(v >> 48);
    p[2] = static_cast<std::uint8_t>(v >> 40);
    p[3] = static_cast<std::uint8_t>(v >> 32);
    p[4] = static_cast<std::uint8_t>(v >> 24);
    p[5] = static_cast<std::uint8_t>(v >> 16);
    p[6] = static_cast<std::uint8_t>(v >> 8);
    p[7] = static_cast<std::uint8_t>(v);
}

// Private SHA-256 context (CR-004 §5). Not exposed in the public header; first
// version provides only the one-shot API. All state is per-call (stack-local),
// with no global mutable buffer, so it is reentrant and thread-safe.
class Sha256Context {
public:
    Sha256Context() : m_bitlen(0), m_buffer_len(0) {
        std::memcpy(m_state, kInitState, sizeof(m_state));
    }

    // Append input bytes. size == 0 is a no-op and does not dereference data.
    // Caller guarantees total bytes <= kMaxInputBytes (bit length fits uint64).
    void update(const std::uint8_t* data, std::size_t size) {
        if (size == 0) return;
        m_bitlen += static_cast<std::uint64_t>(size) * 8u;
        while (size > 0) {
            std::size_t to_copy = 64u - m_buffer_len;
            if (to_copy > size) to_copy = size;
            std::memcpy(m_buffer + m_buffer_len, data, to_copy);
            m_buffer_len += to_copy;
            data += to_copy;
            size -= to_copy;
            if (m_buffer_len == 64u) {
                processBlock(m_buffer);
                m_buffer_len = 0;
            }
        }
    }

    // Append FIPS 180-4 padding and emit the 32-byte big-endian digest.
    Sha256Digest finalize() {
        m_buffer[m_buffer_len++] = 0x80u;
        if (m_buffer_len > 56u) {
            while (m_buffer_len < 64u) m_buffer[m_buffer_len++] = 0u;
            processBlock(m_buffer);
            m_buffer_len = 0;
        }
        while (m_buffer_len < 56u) m_buffer[m_buffer_len++] = 0u;
        storeBe64(m_buffer + 56u, m_bitlen);
        processBlock(m_buffer);

        Sha256Digest out;
        for (int i = 0; i < 8; ++i) storeBe32(out.data() + i * 4u, m_state[i]);
        return out;
    }

private:
    void processBlock(const std::uint8_t* block) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = loadBe32(block + i * 4u);
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 =
                rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 =
                rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = m_state[0], b = m_state[1], c = m_state[2],
                      d = m_state[3], e = m_state[4], f = m_state[5],
                      g = m_state[6], h = m_state[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + S1 + ch + kRoundK[i] + w[i];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    std::uint32_t m_state[8];
    std::uint8_t m_buffer[64];
    std::uint64_t m_bitlen;
    std::size_t m_buffer_len;
};

constexpr char kHexTable[] = "0123456789abcdef";

}  // namespace

Sha256Digest sha256(ByteView input) {
    // CR-004 §7: validate before computation; never return a partial digest.
    if (input.size > 0 && input.data == nullptr) {
        throw HashException(
            "CCU-FW-0401",
            "sha256: non-zero input length with null data pointer");
    }
    if (input.size > kMaxInputBytes) {
        throw HashException(
            "CCU-FW-0401",
            "sha256: input bit length overflows 64-bit representation");
    }

    Sha256Context ctx;
    try {
        ctx.update(input.data, input.size);
        return ctx.finalize();
    } catch (const std::exception&) {
        // CR-004 §7: internal invariant / self-check / backend failure. The
        // self-contained Core performs only arithmetic + memcpy (no throw in
        // practice); this maps any unexpected failure to CCU-FW-0402 and never
        // returns a partial digest.
        throw HashException("CCU-FW-0402", "sha256: internal computation failure");
    }
}

std::string sha256_hex(ByteView input) {
    const Sha256Digest d = sha256(input);  // validates input; may throw 0401
    std::string out(64, '\0');
    for (std::size_t i = 0; i < 32; ++i) {
        out[i * 2] = kHexTable[(d[i] >> 4) & 0x0f];
        out[i * 2 + 1] = kHexTable[d[i] & 0x0f];
    }
    return out;
}

}  // namespace ccu::fw::hash
