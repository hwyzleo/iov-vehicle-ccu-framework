// tests/sha256_test.cpp - standard vectors, padding/binary boundaries and
// error handling for ccu-framework-hash (CCU-FW-DSN-CR-004 §9.1/§9.2).
//
// Expected digests were generated with a trusted OpenSSL oracle and are
// hard-coded here so this test has no runtime dependency on openssl. The
// differential test (sha256_differential_test.cpp) re-checks against the
// oracle for fixed and random inputs.

#include "ccu/fw/hash/sha256.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace ccu::fw::hash;

namespace {

// ---- helpers ---------------------------------------------------------------

bool hexEq(const Sha256Digest& d, const char* hex) {
    if (std::strlen(hex) != 64) return false;
    for (std::size_t i = 0; i < 32; ++i) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        int hi = nib(hex[i * 2]);
        int lo = nib(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        if (d[i] != static_cast<std::uint8_t>((hi << 4) | lo)) return false;
    }
    return true;
}

Sha256Digest sha256Bytes(const std::uint8_t* p, std::size_t n) {
    return sha256(ByteView{p, n});
}

std::string hexOf(const std::uint8_t* p, std::size_t n) {
    return sha256_hex(ByteView{p, n});
}

// ---- standard vectors (CR-004 §9.1) ----------------------------------------

void test_empty_input() {
    auto d = sha256(ByteView{nullptr, 0});
    assert(hexEq(d,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    // string_view empty overload
    auto d2 = sha256(std::string_view{});
    assert(hexEq(d2,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    std::cout << "  [PASS] test_empty_input" << std::endl;
}

void test_abc() {
    auto d = sha256(std::string_view{"abc"});
    assert(hexEq(d,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    std::cout << "  [PASS] test_abc" << std::endl;
}

void test_abcdbcd_block_boundary() {
    auto d = sha256(std::string_view{
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"});
    assert(hexEq(d,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    std::cout << "  [PASS] test_abcdbcd_block_boundary" << std::endl;
}

void test_two_block_nist() {
    auto d = sha256(std::string_view{
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
        "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"});
    assert(hexEq(d,
        "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"));
    std::cout << "  [PASS] test_two_block_nist" << std::endl;
}

void test_million_a() {
    std::string big(1000000, 'a');
    auto d = sha256(std::string_view{big});
    assert(hexEq(d,
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    std::cout << "  [PASS] test_million_a" << std::endl;
}

// ---- padding & block boundaries (CR-004 §9.2) -----------------------------
// Lengths 0..55 fit padding in block 1; 56..63 force a second padding block;
// 64 is an exact block; 65..127 span two data blocks; 128 is two exact blocks.
void test_padding_boundaries() {
    struct Case { std::size_t n; const char* hex; };
    const Case cases[] = {
        {1,   "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"},
        {55,  "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56,  "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {63,  "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64,  "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65,  "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {127, "c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a"},
        {128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"},
    };
    for (const auto& c : cases) {
        std::string in(c.n, 'a');
        auto d = sha256(std::string_view{in});
        assert(hexEq(d, c.hex));
    }
    std::cout << "  [PASS] test_padding_boundaries" << std::endl;
}

// ---- binary safety (CR-004 §9.2) -------------------------------------------

void test_single_null_byte() {
    std::uint8_t b = 0x00;
    auto d = sha256Bytes(&b, 1);
    assert(hexEq(d,
        "6e340b9cffb37a989ca544e6bb780a2c78901d3fb33738768511a30617afa01d"));
    std::cout << "  [PASS] test_single_null_byte" << std::endl;
}

void test_internal_null_not_truncated() {
    // "a\x00b": internal NUL must not terminate processing.
    std::uint8_t b[] = {'a', 0x00, 'b'};
    auto d = sha256Bytes(b, 3);
    assert(hexEq(d,
        "59b271ae1bbcb1d31d41929817f4b16fb439eb4f31520b5ad1d5ce98920a7138"));
    // string_view also sees the full 3 bytes.
    auto d2 = sha256(std::string_view{reinterpret_cast<const char*>(b), 3});
    assert(hexEq(d2,
        "59b271ae1bbcb1d31d41929817f4b16fb439eb4f31520b5ad1d5ce98920a7138"));
    std::cout << "  [PASS] test_internal_null_not_truncated" << std::endl;
}

void test_all_byte_values() {
    std::uint8_t b[256];
    for (int i = 0; i < 256; ++i) b[i] = static_cast<std::uint8_t>(i);
    auto d = sha256Bytes(b, 256);
    assert(hexEq(d,
        "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880"));
    std::cout << "  [PASS] test_all_byte_values" << std::endl;
}

void test_non_utf8() {
    std::uint8_t b[] = {0xff, 0xfe, 0xfd, 0x80, 0x81, 0xc0};
    auto d = sha256Bytes(b, sizeof(b));
    assert(hexEq(d,
        "4388c61a6299fb999be2b4250fc24432b44d4ba9d563dadfd01db529d02cfca5"));
    std::cout << "  [PASS] test_non_utf8" << std::endl;
}

void test_binary_padding_boundaries() {
    // 55-byte binary (padding fits in block 1).
    std::uint8_t b55[55];
    for (std::size_t i = 0; i < 55; ++i)
        b55[i] = static_cast<std::uint8_t>((i * 7) % 256);
    assert(hexEq(sha256Bytes(b55, 55),
        "576a1bf8d4478657e6dc4af9398544765c2a92cde28478b019235cfed315fc09"));
    // 64-byte binary (exact block).
    std::uint8_t b64[64];
    for (std::size_t i = 0; i < 64; ++i)
        b64[i] = static_cast<std::uint8_t>((i * 13) % 256);
    assert(hexEq(sha256Bytes(b64, 64),
        "11e4e9b0dce58ae9cde19cd8ed7f5f6d7c7a156c055f4afd00dcef357d30e372"));
    std::cout << "  [PASS] test_binary_padding_boundaries" << std::endl;
}

void test_unaligned_start() {
    // The Core reads bytes via explicit shifts (no unaligned pointer casts),
    // so hashing the same bytes from a misaligned start must equal hashing
    // them from an aligned copy. A 200-byte content is hashed from buf+off
    // (off=0..3, i.e. alignment 0/1/2/3) and compared with an alignas(64)
    // copy of the identical bytes.
    std::uint8_t buf[208] = {};
    for (std::size_t i = 0; i < sizeof(buf); ++i)
        buf[i] = static_cast<std::uint8_t>((i * 31 + 7) % 256);
    const std::size_t len = 200;
    for (std::size_t off = 0; off <= 3; ++off) {
        alignas(64) std::uint8_t aligned[200];
        std::memcpy(aligned, buf + off, len);
        const auto d_direct = sha256Bytes(buf + off, len);
        const auto d_aligned = sha256Bytes(aligned, len);
        assert(d_direct == d_aligned);
    }
    std::cout << "  [PASS] test_unaligned_start" << std::endl;
}

// ---- error handling (CR-004 §7/§9.2) ---------------------------------------

void test_null_nonzero_throws_0401() {
    bool threw = false;
    try {
        sha256(ByteView{nullptr, 1});
    } catch (const HashException& e) {
        threw = true;
        assert(e.code == "CCU-FW-0401");
    }
    assert(threw);
    std::cout << "  [PASS] test_null_nonzero_throws_0401" << std::endl;
}

void test_bit_length_overflow_throws_0401() {
    // size beyond UINT64_MAX/8 bytes cannot have its bit length represented in
    // 64 bits. Validation rejects before any byte is read, so a non-null (but
    // never dereferenced) pointer is safe.
    const std::uint64_t huge =
        (UINT64_MAX / 8u) + 1u;  // > kMaxInputBytes
    std::uint8_t dummy = 0;
    bool threw = false;
    try {
        sha256(ByteView{&dummy, static_cast<std::size_t>(huge)});
    } catch (const HashException& e) {
        threw = true;
        assert(e.code == "CCU-FW-0401");
    }
    assert(threw);
    std::cout << "  [PASS] test_bit_length_overflow_throws_0401" << std::endl;
}

void test_empty_null_pointer_is_legal() {
    // Reaffirm: ByteView{nullptr, 0} is a valid empty input (not an error).
    auto d = sha256(ByteView{nullptr, 0});
    assert(hexEq(d,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    std::cout << "  [PASS] test_empty_null_pointer_is_legal" << std::endl;
}

// ---- raw/Hex consistency (CR-004 §6/§9.2) ----------------------------------

void test_raw_fixed_32_bytes() {
    std::uint8_t b[] = {0, 1, 2, 3, 4};
    auto d = sha256Bytes(b, sizeof(b));
    assert(d.size() == 32);
    std::cout << "  [PASS] test_raw_fixed_32_bytes" << std::endl;
}

void test_hex_format_invariants() {
    // Hex is exactly 64 chars, all lowercase [0-9a-f], no prefix/separator/ws.
    std::uint8_t b[] = {0xde, 0xad, 0xbe, 0xef};
    std::string h = hexOf(b, sizeof(b));
    assert(h.size() == 64);
    for (char c : h) {
        assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    // empty input hex
    std::string he = sha256_hex(ByteView{nullptr, 0});
    assert(he == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std::cout << "  [PASS] test_hex_format_invariants" << std::endl;
}

void test_raw_hex_equivalence() {
    // sha256_hex(input) must equal a fixed lowercase hex encoding of
    // sha256(input), decoupling the algorithm from the display format.
    std::string inputs[] = {
        "", "abc", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        std::string(200, 'z'), std::string("a\x00b\xc3\x28", 5),
    };
    for (const auto& s : inputs) {
        auto d = sha256(std::string_view{s});
        std::string h = sha256_hex(std::string_view{s});
        assert(h.size() == 64);
        for (std::size_t i = 0; i < 32; ++i) {
            auto nib = [](std::uint8_t v, int hi) -> char {
                int n = hi ? (v >> 4) & 0x0f : v & 0x0f;
                return static_cast<char>(n < 10 ? '0' + n : 'a' + (n - 10));
            };
            assert(h[i * 2] == nib(d[i], true));
            assert(h[i * 2 + 1] == nib(d[i], false));
        }
    }
    std::cout << "  [PASS] test_raw_hex_equivalence" << std::endl;
}

void test_byteview_and_string_view_agree() {
    std::string s = "the quick brown fox";
    auto d1 = sha256(std::string_view{s});
    auto d2 = sha256(ByteView{
        reinterpret_cast<const std::uint8_t*>(s.data()), s.size()});
    assert(d1 == d2);
    assert(sha256_hex(std::string_view{s}) ==
           sha256_hex(ByteView{
               reinterpret_cast<const std::uint8_t*>(s.data()), s.size()}));
    std::cout << "  [PASS] test_byteview_and_string_view_agree" << std::endl;
}

// Minimal consumer-contract demonstration (CR-004 §13): a consumer selects
// fields, stable-sorts them, canonicalizes to a deterministic byte sequence,
// then hashes and compares. Per §9.4 the framework only receives the final
// canonical bytes; field-selection/sorting rules themselves live in consumer
// tests. This case proves the canonical-bytes -> SHA-256 -> compare flow.
void test_consumer_contract_demo() {
    // Simulated canonicalization: fields sorted by key, "key=value" joined by
    // '|'. The framework sees only the resulting bytes.
    std::string canonical = "seq=42|vin=XYZ";
    auto d = sha256(std::string_view{canonical});
    assert(hexEq(d,
        "e6663bec0227eb78beb97f0d4225cbabc7f72ee7bada8f546f9e14f5539387b6"));
    // Equal canonical sequences yield equal digests.
    std::string same = "seq=42|vin=XYZ";
    assert(sha256(std::string_view{canonical}) ==
           sha256(std::string_view{same}));
    // A different canonical sequence yields a different digest.
    std::string other = "seq=43|vin=XYZ";
    assert(sha256(std::string_view{canonical}) !=
           sha256(std::string_view{other}));
    std::cout << "  [PASS] test_consumer_contract_demo" << std::endl;
}

}  // namespace

int main() {
    test_empty_input();
    test_abc();
    test_abcdbcd_block_boundary();
    test_two_block_nist();
    test_million_a();
    test_padding_boundaries();
    test_single_null_byte();
    test_internal_null_not_truncated();
    test_all_byte_values();
    test_non_utf8();
    test_binary_padding_boundaries();
    test_unaligned_start();
    test_null_nonzero_throws_0401();
    test_bit_length_overflow_throws_0401();
    test_empty_null_pointer_is_legal();
    test_raw_fixed_32_bytes();
    test_hex_format_invariants();
    test_raw_hex_equivalence();
    test_byteview_and_string_view_agree();
    test_consumer_contract_demo();
    std::cout << "All sha256 tests passed." << std::endl;
    return 0;
}
