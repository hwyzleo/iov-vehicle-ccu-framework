// tests/sha256_differential_test.cpp - differential & cross-platform testing
// for ccu-framework-hash (CCU-FW-DSN-CR-004 §9.3).
//
// Uses the OpenSSL CLI as a trusted oracle. The oracle is a TEST-ONLY
// dependency: it MUST NOT become a product runtime dependency (CR-004 §9.3/§11).
// Input bytes are written to a temp file (binary-safe, no shell escaping of
// payload) and digested via `openssl dgst -sha256`; the framework's raw digest
// and Hex output are both compared against the oracle. If openssl is not on
// PATH the test is SKIPPED (returns 0) so the suite stays green on minimal
// hosts while still running fully where the oracle exists.

#include "ccu/fw/hash/sha256.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

using namespace ccu::fw::hash;

namespace {

// ---- oracle ----------------------------------------------------------------

bool opensslAvailable() {
    FILE* p = popen("openssl version", "r");
    if (!p) return false;
    char buf[128];
    bool ok = std::fgets(buf, sizeof(buf), p) != nullptr;
    pclose(p);
    return ok;
}

// Digest `data`/`size` via the OpenSSL CLI oracle, returning lowercase hex.
// Returns empty string on failure.
std::string oracleHex(const std::uint8_t* data, std::size_t size) {
    char path[] = "/tmp/ccu-hash-or-XXXXXX";
    int fd = ::mkstemp(path);
    if (fd < 0) return "";
    {
        std::ofstream f(path, std::ios::binary);
        if (!f) {
            ::close(fd);
            ::unlink(path);
            return "";
        }
        f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!f) {
            ::close(fd);
            ::unlink(path);
            return "";
        }
        f.close();
    }
    ::close(fd);

    // Path from mkstemp has no shell metacharacters; single-quote for safety.
    std::string cmd = "openssl dgst -sha256 '";
    cmd += path;
    cmd += "' 2>&1";

    FILE* p = popen(cmd.c_str(), "r");
    std::string out;
    if (p) {
        char buf[256];
        while (std::fgets(buf, sizeof(buf), p)) out += buf;
        pclose(p);
    }
    ::unlink(path);

    // Output: "SHA256(<path>)= <hex>\n". Take the 64 hex chars after "= ".
    auto pos = out.rfind("= ");
    if (pos == std::string::npos) return "";
    std::string hex = out.substr(pos + 2);
    // trim trailing whitespace/newline
    while (!hex.empty() &&
           (hex.back() == '\n' || hex.back() == '\r' || hex.back() == ' '))
        hex.pop_back();
    if (hex.size() != 64) return "";
    return hex;
}

// Convert a raw 32-byte digest to lowercase hex (independent of the framework's
// own sha256_hex, so the raw path is validated separately from the Hex path).
std::string rawToHex(const Sha256Digest& d) {
    static const char* tbl = "0123456789abcdef";
    std::string s(64, '\0');
    for (std::size_t i = 0; i < 32; ++i) {
        s[i * 2] = tbl[(d[i] >> 4) & 0x0f];
        s[i * 2 + 1] = tbl[d[i] & 0x0f];
    }
    return s;
}

bool checkOne(const std::uint8_t* data, std::size_t size, std::size_t idx) {
    const std::string oracle = oracleHex(data, size);
    if (oracle.empty()) {
        std::cerr << "  [FAIL] oracle unavailable/failed for case " << idx
                  << " (size=" << size << ")" << std::endl;
        return false;
    }
    const Sha256Digest raw = sha256(ByteView{data, size});
    const std::string hex = sha256_hex(ByteView{data, size});
    if (rawToHex(raw) != oracle) {
        std::cerr << "  [FAIL] raw mismatch case " << idx << " size=" << size
                  << "\n    oracle=" << oracle
                  << "\n    raw   =" << rawToHex(raw) << std::endl;
        return false;
    }
    if (hex != oracle) {
        std::cerr << "  [FAIL] hex mismatch case " << idx << " size=" << size
                  << "\n    oracle=" << oracle
                  << "\n    hex   =" << hex << std::endl;
        return false;
    }
    return true;
}

// ---- fixed inputs ----------------------------------------------------------

bool test_fixed_inputs() {
    std::vector<std::vector<std::uint8_t>> inputs;
    inputs.emplace_back();                                         // empty
    inputs.emplace_back(std::vector<std::uint8_t>{0x61, 0x62, 0x63}); // "abc"
    // padding/block boundary lengths of 'a'
    for (std::size_t n : {1u, 55u, 56u, 63u, 64u, 65u, 127u, 128u, 256u})
        inputs.emplace_back(std::vector<std::uint8_t>(n, 0x61));
    // all byte values 0..255
    {
        std::vector<std::uint8_t> b(256);
        for (int i = 0; i < 256; ++i) b[i] = static_cast<std::uint8_t>(i);
        inputs.push_back(std::move(b));
    }
    // binary with internal NUL and non-UTF-8
    inputs.emplace_back(std::vector<std::uint8_t>{0x00});
    inputs.emplace_back(std::vector<std::uint8_t>{'a', 0x00, 'b'});
    inputs.emplace_back(std::vector<std::uint8_t>{0xff, 0xfe, 0xfd, 0x80, 0x81, 0xc0});
    // a longer pseudo-random binary blob (multi-block)
    {
        std::vector<std::uint8_t> b(1000);
        for (std::size_t i = 0; i < b.size(); ++i)
            b[i] = static_cast<std::uint8_t>((i * 1103515245u + 12345u) % 256);
        inputs.push_back(std::move(b));
    }

    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (!checkOne(inputs[i].data(), inputs[i].size(), i)) return false;
    }
    std::cout << "  [PASS] test_fixed_inputs (" << inputs.size() << " cases)"
              << std::endl;
    return true;
}

// ---- random differential ---------------------------------------------------

bool test_random_inputs() {
    std::mt19937 rng(0xc0ffeeu);  // fixed seed: reproducible failures
    std::uniform_int_distribution<int> lenDist(0, 512);
    std::uniform_int_distribution<int> byteDist(0, 255);
    const int N = 500;
    for (int i = 0; i < N; ++i) {
        std::size_t n = static_cast<std::size_t>(lenDist(rng));
        std::vector<std::uint8_t> b(n);
        for (std::size_t j = 0; j < n; ++j)
            b[j] = static_cast<std::uint8_t>(byteDist(rng));
        if (!checkOne(b.data(), b.size(), i)) return false;
    }
    std::cout << "  [PASS] test_random_inputs (" << N << " cases)" << std::endl;
    return true;
}

}  // namespace

int main() {
    if (!opensslAvailable()) {
        std::cout << "  [SKIPPED] openssl oracle not found on PATH" << std::endl;
        return 0;
    }
    if (!test_fixed_inputs()) return 1;
    if (!test_random_inputs()) return 1;
    std::cout << "All sha256 differential tests passed." << std::endl;
    return 0;
}
