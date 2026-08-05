#include "config/config_snapshot_impl.h"
#include "config/config_node.h"
#include "config_types.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace cgw::fw::config;
using cgw::fw::config::detail::SnapshotImpl;

static NodePtr scalar(const std::string& s) { return ConfigNode::makeScalar(s); }
static NodePtr nullNode() { return ConfigNode::makeNull(); }
static NodePtr seq(std::initializer_list<NodePtr> items) {
    auto n = ConfigNode::makeSequence();
    for (auto& e : items) n->sequence.push_back(e);
    return n;
}
static NodePtr map(std::initializer_list<std::pair<std::string, NodePtr>> items) {
    auto n = ConfigNode::makeMap();
    for (auto& e : items) n->map.emplace_back(e.first, e.second);
    return n;
}

static NodePtr buildSample() {
    return map({
        {"common", map({{"log", map({
            {"level", scalar("INFO")},
            {"schema_version", scalar("1")},
            {"enabled", scalar("true")},
            {"ratio", scalar("0.75")},
            {"ports", seq({scalar("8080"), scalar("9090")})},
        })}})},
        {"diag", map({
            {"ecus", seq({scalar("BMS"), scalar("VCU")})},
            {"disabled", nullNode()},
        })},
    });
}

void test_has_and_get_scalar() {
    SnapshotImpl s(buildSample());
    assert(s.has("common.log.level"));
    assert(s.getString("common.log.level") == "INFO");
    std::cout << "  [PASS] test_has_and_get_scalar" << std::endl;
}

void test_get_int_double_bool() {
    SnapshotImpl s(buildSample());
    assert(s.getInt64("common.log.schema_version") == 1);
    assert(s.getBool("common.log.enabled") == true);
    // double compare
    double r = s.getDouble("common.log.ratio");
    assert(r > 0.74 && r < 0.76);
    std::cout << "  [PASS] test_get_int_double_bool" << std::endl;
}

void test_get_vectors() {
    SnapshotImpl s(buildSample());
    auto ports = s.getInt64Vec("common.log.ports");
    assert(ports.size() == 2 && ports[0] == 8080 && ports[1] == 9090);
    auto ecus = s.getStringVec("diag.ecus");
    assert(ecus.size() == 2 && ecus[0] == "BMS" && ecus[1] == "VCU");
    std::cout << "  [PASS] test_get_vectors" << std::endl;
}

void test_explicit_null_present_but_unreadable() {
    SnapshotImpl s(buildSample());
    assert(s.has("diag.disabled"));          // explicit null counts as present
    bool threw = false;
    try { s.getInt64("diag.disabled"); } catch (const ConfigException& e) {
        threw = true; assert(e.code == "CGW-FW-0004");
    }
    assert(threw);
    std::cout << "  [PASS] test_explicit_null_present_but_unreadable" << std::endl;
}

void test_missing_key_throws_0003() {
    SnapshotImpl s(buildSample());
    assert(!s.has("common.log.nope"));
    bool threw = false;
    try { s.getString("common.log.nope"); } catch (const ConfigException& e) {
        threw = true; assert(e.code == "CGW-FW-0003");
    }
    assert(threw);
    std::cout << "  [PASS] test_missing_key_throws_0003" << std::endl;
}

void test_type_mismatch_throws_0004() {
    SnapshotImpl s(buildSample());
    bool threw = false;
    try { s.getInt64("common.log.level"); } catch (const ConfigException& e) {
        threw = true; assert(e.code == "CGW-FW-0004");
    }
    assert(threw);
    // vector requested on scalar
    threw = false;
    try { s.getStringVec("common.log.level"); } catch (const ConfigException& e) {
        threw = true; assert(e.code == "CGW-FW-0004");
    }
    assert(threw);
    std::cout << "  [PASS] test_type_mismatch_throws_0004" << std::endl;
}

void test_keys_section() {
    SnapshotImpl s(buildSample());
    auto k = s.keys("common.log");
    assert(k.size() == 5);
    // contains level
    bool found = false;
    for (auto& x : k) if (x == "level") found = true;
    assert(found);
    // non-map section -> empty
    assert(s.keys("common.log.level").empty());
    std::cout << "  [PASS] test_keys_section" << std::endl;
}

void test_hex_and_dotted_navigation() {
    SnapshotImpl s(map({{"svc", map({{"id", scalar("0x1110")}})}}));
    assert(s.getInt64("svc.id") == 0x1110);
    std::cout << "  [PASS] test_hex_and_dotted_navigation" << std::endl;
}

int main() {
    std::cout << "Running ConfigSnapshot tests..." << std::endl;
    test_has_and_get_scalar();
    test_get_int_double_bool();
    test_get_vectors();
    test_explicit_null_present_but_unreadable();
    test_missing_key_throws_0003();
    test_type_mismatch_throws_0004();
    test_keys_section();
    test_hex_and_dotted_navigation();
    std::cout << "All ConfigSnapshot tests passed!" << std::endl;
    return 0;
}
