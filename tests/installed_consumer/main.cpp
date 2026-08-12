// Independent installed-package consumer (CGW-FW-DSN-CR-006 §8).
//
// Verifies that the installed CGWFramework package resolves all five exported
// targets and that their dependency closure (yaml-cpp, pthread) links cleanly.
// Exercises:
//   - cgw-framework-hash : concrete, verifiable SHA-256 known-answer test.
//   - cgw-framework-config : Config::load throws for a missing service, which
//     pulls in the yaml-cpp link path (the static PRIVATE dependency).
//   - cgw-framework-log / store / someip : symbol addresses are referenced so
//     the linker extracts their archive members (side-effect-free).

#include "cgw/fw/hash/sha256.hpp"
#include "cgw/fw/hash/hex.hpp"
#include "config.h"
#include "log.h"
#include "store.h"
#include "cgw/fw/someip/runtime.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

int main() {
    int rc = 0;

    // --- hash: verifiable known-answer test (FIPS 180-4 "abc") ---
    std::string digest = cgw::fw::hash::sha256_hex("abc");
    const char* expected =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    if (digest != expected) {
        std::fprintf(stderr, "consumer: sha256_hex(\"abc\") = %s (expected %s)\n",
                     digest.c_str(), expected);
        rc = 1;
    }

    // --- hash hex: minimal round-trip (CR-007 §8.4) ---
    // Binary-safe: internal NUL and high byte survive encode/decode.
    {
        const std::string bin = std::string("a\x00\xff", 3);
        const std::string hex = cgw::fw::hash::bytesToHex(bin);
        const std::string back = cgw::fw::hash::hexToBytes(hex);
        if (hex != "6100ff" || back != bin) {
            std::fprintf(stderr, "consumer: hex round-trip failed (%s)\n",
                         hex.c_str());
            rc = 1;
        }
    }

    // --- config: exercises the yaml-cpp link path; throws for missing roots ---
    try {
        cgw::fw::config::Config::load("cgw-framework-consumer-nonexistent");
        std::fprintf(stderr, "consumer: expected Config::load to throw\n");
        rc = 1;
    } catch (const std::exception&) {
        // expected: no config roots resolve for a nonexistent service
    }

    // --- log / store / someip: reference symbols to force archive extraction ---
    auto p_init   = &cgw::fw::log::Logger::init;
    auto p_open   = &cgw::fw::store::Store::open;
    auto p_create = &cgw::fw::someip::SomeIpRuntime::create;
    if (reinterpret_cast<std::uintptr_t>(p_init) == 0 ||
        reinterpret_cast<std::uintptr_t>(p_open) == 0 ||
        reinterpret_cast<std::uintptr_t>(p_create) == 0) {
        std::fprintf(stderr, "consumer: unresolved symbol address\n");
        rc = 1;
    }

    if (rc == 0) {
        std::printf("consumer: OK (hash + config + log + store + someip linked)\n");
    }
    return rc;
}
