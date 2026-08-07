#include "store_types.h"
#include <cassert>
#include <iostream>

using namespace cgw::fw::store;
namespace d = cgw::fw::store::detail;

void test_string_roundtrip() {
    std::string v = "hello \xe4\xb8\xad world"; // UTF-8
    auto b = d::encode(v);
    assert(d::decodeString(b) == v);
    std::cout << "  [PASS] test_string_roundtrip" << std::endl;
}

void test_int64_roundtrip() {
    int64_t v = -1234567890123LL;
    auto b = d::encode(v);
    assert(d::decodeInt64(b) == v);
    // boundary
    auto b2 = d::encode((int64_t)0);
    assert(d::decodeInt64(b2) == 0);
    auto b3 = d::encode((int64_t)INT64_MIN);
    assert(d::decodeInt64(b3) == INT64_MIN);
    std::cout << "  [PASS] test_int64_roundtrip" << std::endl;
}

void test_double_roundtrip() {
    double v = 3.14159265358979;
    auto b = d::encode(v);
    assert(d::decodeDouble(b) == v);
    std::cout << "  [PASS] test_double_roundtrip" << std::endl;
}

void test_bool_roundtrip() {
    auto bt = d::encode(true);
    auto bf = d::encode(false);
    assert(d::decodeBool(bt) == true);
    assert(d::decodeBool(bf) == false);
    std::cout << "  [PASS] test_bool_roundtrip" << std::endl;
}

void test_string_vec_roundtrip() {
    std::vector<std::string> v{"a", "", "cc", "dddd"};
    auto b = d::encode(v);
    assert(d::decodeStringVec(b) == v);
    std::cout << "  [PASS] test_string_vec_roundtrip" << std::endl;
}

void test_int64_vec_roundtrip() {
    std::vector<int64_t> v{1, -2, 3, 0, INT64_MAX};
    auto b = d::encode(v);
    assert(d::decodeInt64Vec(b) == v);
    std::cout << "  [PASS] test_int64_vec_roundtrip" << std::endl;
}

void test_type_mismatch() {
    auto b = d::encode((int64_t)42);
    bool threw = false;
    try {
        d::decodeString(b); // stored as Int64, read as String
    } catch (const StoreException& e) {
        threw = true;
        assert(e.code == "CGW-FW-0104");
    }
    assert(threw);
    std::cout << "  [PASS] test_type_mismatch" << std::endl;
}

void test_bad_magic() {
    std::vector<uint8_t> b = d::encode((int64_t)42);
    b[0] = 'X'; // corrupt magic
    bool threw = false;
    try {
        d::decodeInt64(b);
    } catch (const StoreException& e) {
        threw = true;
        assert(e.code == "CGW-FW-0104");
    }
    assert(threw);
    std::cout << "  [PASS] test_bad_magic" << std::endl;
}

void test_truncated() {
    std::vector<uint8_t> b = d::encode(std::string("hello"));
    b.resize(5); // truncate header
    bool threw = false;
    try {
        d::decodeString(b);
    } catch (const StoreException& e) {
        threw = true;
        assert(e.code == "CGW-FW-0104");
    }
    assert(threw);
    std::cout << "  [PASS] test_truncated" << std::endl;
}

void test_corrupted_length() {
    std::vector<uint8_t> b = d::encode(std::string("hi"));
    // Tamper with the length field (offset 8) to claim a huge length.
    b[8] = 0xff; b[9] = 0xff; b[10] = 0xff; b[11] = 0x7f;
    bool threw = false;
    try {
        d::decodeString(b);
    } catch (const StoreException& e) {
        threw = true;
        assert(e.code == "CGW-FW-0104");
    }
    assert(threw);
    std::cout << "  [PASS] test_corrupted_length" << std::endl;
}

int main() {
    test_string_roundtrip();
    test_int64_roundtrip();
    test_double_roundtrip();
    test_bool_roundtrip();
    test_string_vec_roundtrip();
    test_int64_vec_roundtrip();
    test_type_mismatch();
    test_bad_magic();
    test_truncated();
    test_corrupted_length();
    std::cout << "All store_serializer tests passed." << std::endl;
    return 0;
}
