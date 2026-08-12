// tests/hex_test.cpp - Golden, round-trip, case, error and boundary tests for
// the cgw-framework-hash byte<->Hex codec (CGW-FW-DSN-CR-007 §8).
//
// Covers §8.1 Golden, §8.2 round-trip + case, §8.3 error/boundary and a
// §8.4 regression that sha256()/sha256_hex() remain unchanged. The codec is
// pure and self-contained; no oracle is required.

#include "cgw/fw/hash/hex.hpp"
#include "cgw/fw/hash/sha256.hpp"  // ByteView + regression

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using namespace cgw::fw::hash;

namespace {

// ---- helpers ---------------------------------------------------------------

bool bytesEq(const std::string& s, const std::uint8_t* p, std::size_t n) {
    return s.size() == n && std::memcmp(s.data(), p, n) == 0;
}

// ---- §8.1 Golden -----------------------------------------------------------

void test_golden_encode() {
    assert(bytesToHex(ByteView{nullptr, 0}) == "");
    {
        std::uint8_t b[] = {0x00};
        assert(bytesToHex(ByteView{b, 1}) == "00");
    }
    {
        std::uint8_t b[] = {0xff};
        assert(bytesToHex(ByteView{b, 1}) == "ff");
    }
    {
        std::uint8_t b[] = {0x00, 0x7f, 0x80, 0xff};
        assert(bytesToHex(ByteView{b, 4}) == "007f80ff");
    }
    {
        std::uint8_t b[] = {0x48, 0x65, 0x78, 0x00, 0xff};
        assert(bytesToHex(ByteView{b, 5}) == "48657800ff");
    }
    std::cout << "  [PASS] test_golden_encode" << std::endl;
}

void test_golden_decode() {
    assert(hexToBytes("") == "");
    {
        std::uint8_t e[] = {0x00};
        assert(bytesEq(hexToBytes("00"), e, 1));
    }
    {
        std::uint8_t e[] = {0xff};
        assert(bytesEq(hexToBytes("ff"), e, 1));
    }
    {
        std::uint8_t e[] = {0x00, 0x7f, 0x80, 0xff};
        assert(bytesEq(hexToBytes("007f80ff"), e, 4));
    }
    {
        std::uint8_t e[] = {0x48, 0x65, 0x78, 0x00, 0xff};
        assert(bytesEq(hexToBytes("48657800ff"), e, 5));
    }
    std::cout << "  [PASS] test_golden_decode" << std::endl;
}

// ---- §8.2 round-trip + case ------------------------------------------------

void test_round_trip_all_bytes() {
    std::uint8_t all[256];
    for (int i = 0; i < 256; ++i) all[i] = static_cast<std::uint8_t>(i);
    const std::string hex = bytesToHex(ByteView{all, 256});
    assert(hex.size() == 512);
    // Output is lowercase [0-9a-f] only.
    for (char c : hex) {
        assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    const std::string back = hexToBytes(hex);
    assert(bytesEq(back, all, 256));
    std::cout << "  [PASS] test_round_trip_all_bytes" << std::endl;
}

void test_round_trip_fixed_vectors() {
    const std::vector<std::vector<std::uint8_t>> inputs = {
        {},
        {0x00},
        {0xff},
        {0x00, 0x7f, 0x80, 0xff},
        {0x48, 0x65, 0x78, 0x00, 0xff},
        {0x00, 0x01, 0x02, 0x7e, 0x7f, 0x80, 0xfe, 0xff},
    };
    for (const auto& v : inputs) {
        const std::string hex = bytesToHex(ByteView{v.data(), v.size()});
        assert(hex.size() == v.size() * 2);
        const std::string back = hexToBytes(hex);
        assert(bytesEq(back, v.data(), v.size()));
    }
    std::cout << "  [PASS] test_round_trip_fixed_vectors" << std::endl;
}

void test_round_trip_random() {
    std::mt19937 rng(0xc0ffeeu);  // fixed seed: reproducible failures
    std::uniform_int_distribution<int> lenDist(0, 512);
    std::uniform_int_distribution<int> byteDist(0, 255);
    const int N = 500;
    for (int i = 0; i < N; ++i) {
        const std::size_t n = static_cast<std::size_t>(lenDist(rng));
        std::vector<std::uint8_t> b(n);
        for (std::size_t j = 0; j < n; ++j)
            b[j] = static_cast<std::uint8_t>(byteDist(rng));
        const std::string hex = bytesToHex(ByteView{b.data(), n});
        assert(hex.size() == n * 2);
        for (char c : hex) {
            assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        }
        const std::string back = hexToBytes(hex);
        assert(bytesEq(back, b.data(), n));
    }
    std::cout << "  [PASS] test_round_trip_random (" << N << " cases)"
              << std::endl;
}

void test_case_insensitive_decode() {
    std::uint8_t e[] = {0xa0, 0xff};
    assert(bytesEq(hexToBytes("a0ff"), e, 2));
    assert(bytesEq(hexToBytes("A0FF"), e, 2));
    assert(bytesEq(hexToBytes("a0Ff"), e, 2));
    // All case variants decode to the identical bytes.
    assert(hexToBytes("a0ff") == hexToBytes("A0FF"));
    assert(hexToBytes("a0ff") == hexToBytes("a0Ff"));
    std::cout << "  [PASS] test_case_insensitive_decode" << std::endl;
}

void test_encode_always_lowercase() {
    // bytesToHex(hexToBytes(validHex)) SHALL return the normalized lowercase
    // Hex (CR-007 §8.2).
    assert(bytesToHex(hexToBytes("A0FF")) == "a0ff");
    assert(bytesToHex(hexToBytes("00ABCD")) == "00abcd");
    assert(bytesToHex(hexToBytes("a0Ff")) == "a0ff");
    std::cout << "  [PASS] test_encode_always_lowercase" << std::endl;
}

// ---- §8.3 error & boundary -------------------------------------------------

void test_odd_length_throws_0410() {
    const std::string cases[] = {"0", "abc", "00 01", "00:01"};
    for (const auto& s : cases) {
        bool threw = false;
        try {
            hexToBytes(std::string_view{s});
        } catch (const HexException& e) {
            threw = true;
            assert(e.code() == "CGW-FW-0410");
            assert(e.position() == std::string_view::npos);
        }
        assert(threw);
    }
    std::cout << "  [PASS] test_odd_length_throws_0410" << std::endl;
}

void test_invalid_chars_throw_0411_with_position() {
    struct Case {
        std::string hex;
        std::size_t pos;
    };
    const Case cases[] = {
        {"0x00", 1},   // 'x' at index 1
        {"gg", 0},     // 'g' at index 0
        {"00z0", 2},   // 'z' at index 2
    };
    for (const auto& c : cases) {
        bool threw = false;
        try {
            hexToBytes(std::string_view{c.hex});
        } catch (const HexException& e) {
            threw = true;
            assert(e.code() == "CGW-FW-0411");
            assert(e.position() == c.pos);
        }
        assert(threw);
    }
    // Non-Hex via embedded whitespace/NUL in a string_view (even length).
    {
        char nl[] = {'0', '\n', '0', 'a'};
        bool threw = false;
        try {
            hexToBytes(std::string_view{nl, 4});
        } catch (const HexException& e) {
            threw = true;
            assert(e.code() == "CGW-FW-0411");
            assert(e.position() == 1);
        }
        assert(threw);
    }
    {
        char nul[] = {'0', '\0', '0', 'a'};
        bool threw = false;
        try {
            hexToBytes(std::string_view{nul, 4});
        } catch (const HexException& e) {
            threw = true;
            assert(e.code() == "CGW-FW-0411");
            assert(e.position() == 1);
        }
        assert(threw);
    }
    std::cout << "  [PASS] test_invalid_chars_throw_0411_with_position"
              << std::endl;
}

void test_no_partial_result_on_error() {
    // The result is returned by value; on throw the caller never observes a
    // value (the assignment does not complete).
    bool threw = false;
    try {
        std::string r = hexToBytes("gg");  // throws at pair 0
        (void)r;
        assert(false && "expected HexException");
    } catch (const HexException& e) {
        threw = true;
        assert(e.code() == "CGW-FW-0411");
    }
    assert(threw);
    std::cout << "  [PASS] test_no_partial_result_on_error" << std::endl;
}

void test_byteview_empty_nullptr_legal() {
    assert(bytesToHex(ByteView{nullptr, 0}) == "");
    assert(hexToBytes("") == "");
    std::cout << "  [PASS] test_byteview_empty_nullptr_legal" << std::endl;
}

void test_byteview_nullptr_nonzero_throws_0412() {
    bool threw = false;
    try {
        bytesToHex(ByteView{nullptr, 1});
    } catch (const HexException& e) {
        threw = true;
        assert(e.code() == "CGW-FW-0412");
        assert(e.position() == std::string_view::npos);
    }
    assert(threw);
    std::cout << "  [PASS] test_byteview_nullptr_nonzero_throws_0412"
              << std::endl;
}

void test_encode_length_overflow_throws_0412() {
    // input.size beyond max_size()/2 cannot have its 2*size output represented
    // in a string. Validation rejects before any byte is read, so a non-null
    // (but never dereferenced) pointer is safe.
    const std::size_t huge = std::string{}.max_size() / 2 + 1;
    std::uint8_t dummy = 0;
    bool threw = false;
    try {
        bytesToHex(ByteView{&dummy, huge});
    } catch (const HexException& e) {
        threw = true;
        assert(e.code() == "CGW-FW-0412");
        assert(e.position() == std::string_view::npos);
    }
    assert(threw);
    std::cout << "  [PASS] test_encode_length_overflow_throws_0412"
              << std::endl;
}

void test_string_view_overload_matches_byteview() {
    const std::string s = "the quick brown fox";
    const std::string h1 = bytesToHex(ByteView{
        reinterpret_cast<const std::uint8_t*>(s.data()), s.size()});
    const std::string h2 = bytesToHex(std::string_view{s});
    assert(h1 == h2);
    assert(hexToBytes(h1) == s);
    std::cout << "  [PASS] test_string_view_overload_matches_byteview"
              << std::endl;
}

void test_internal_nul_binary_safe() {
    // Internal NUL and high bytes are processed by explicit length; no
    // truncation, no charset conversion (CR-007 §5/§8.3).
    std::uint8_t b[] = {0x48, 0x65, 0x78, 0x00, 0xff};  // "Hex\0\xff"
    const std::string hex = bytesToHex(ByteView{b, 5});
    assert(hex == "48657800ff");
    const std::string back = hexToBytes(hex);
    assert(bytesEq(back, b, 5));
    // string_view overload also sees the full 5 bytes.
    const std::string sv(reinterpret_cast<const char*>(b), 5);
    assert(bytesToHex(std::string_view{sv}) == "48657800ff");
    std::cout << "  [PASS] test_internal_nul_binary_safe" << std::endl;
}

// ---- §8.4 regression: SHA-256 unchanged ------------------------------------

void test_sha256_regression() {
    // CR-007 §2/§11: sha256()/sha256_hex() behavior is unchanged.
    assert(sha256_hex(std::string_view{"abc"}) ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(sha256_hex(ByteView{nullptr, 0}) ==
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std::cout << "  [PASS] test_sha256_regression" << std::endl;
}

}  // namespace

int main() {
    test_golden_encode();
    test_golden_decode();
    test_round_trip_all_bytes();
    test_round_trip_fixed_vectors();
    test_round_trip_random();
    test_case_insensitive_decode();
    test_encode_always_lowercase();
    test_odd_length_throws_0410();
    test_invalid_chars_throw_0411_with_position();
    test_no_partial_result_on_error();
    test_byteview_empty_nullptr_legal();
    test_byteview_nullptr_nonzero_throws_0412();
    test_encode_length_overflow_throws_0412();
    test_string_view_overload_matches_byteview();
    test_internal_nul_binary_safe();
    test_sha256_regression();
    std::cout << "All hex tests passed." << std::endl;
    return 0;
}
