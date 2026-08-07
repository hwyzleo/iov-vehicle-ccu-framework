// examples/hash_consumer.cpp - minimal CGW consumer sample (CR-004 §13).
//
// Demonstrates the consumer flow: select fields -> stable sort -> canonical
// bytes -> SHA-256 -> compare. Links the exported build-tree alias
// cgw::framework-hash. This is a sample, NOT a unit test; real business
// canonicalization rules belong to each service (CR-004 §8/§9.4).
#include "cgw/fw/hash/sha256.hpp"

#include <iostream>
#include <map>
#include <string>

int main() {
    using cgw::fw::hash::sha256_hex;

    // A consumer selects fields and stable-sorts them (std::map orders by key).
    std::map<std::string, std::string> fields = {
        {"vin", "WBA12345"}, {"seq", "42"}, {"ecu", "BMS"}};

    // Canonicalize: "key=value" joined by '|' (toy rule; services define their
    // own encoding/ordering/delimiter per CR-004 §8).
    std::string canonical;
    bool first = true;
    for (const auto& kv : fields) {
        if (!first) canonical += '|';
        canonical += kv.first + "=" + kv.second;
        first = false;
    }

    const std::string digest = sha256_hex(canonical);
    std::cout << "canonical: " << canonical << "\n"
              << "sha256:    " << digest << std::endl;

    // Re-computing the same canonical bytes yields the same digest (compare).
    if (digest != sha256_hex(canonical)) {
        std::cerr << "digest mismatch on recompute" << std::endl;
        return 1;
    }
    // A different canonicalization yields a different digest.
    if (digest == sha256_hex("ecu=BMS|seq=42|vin=WBA54321")) {
        std::cerr << "unexpected equal digest for different input" << std::endl;
        return 1;
    }
    std::cout << "consumer sample OK" << std::endl;
    return 0;
}
