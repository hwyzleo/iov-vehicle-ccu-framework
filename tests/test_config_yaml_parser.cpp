#include "config/config_yaml_parser.h"
#include "config/config_node.h"
#include "config_types.h"
#include "config_test_util.h"
#include <cassert>
#include <iostream>

using namespace cgw::fw::config;
using cgwtest::TempDir;

void test_valid_map() {
    auto n = YamlParser::parse("common:\n  log:\n    level: INFO\n", "mem");
    assert(n->type == NodeType::kMap);
    const auto* common = n->find("common");
    assert(common && common->type == NodeType::kMap);
    const auto* log = common->find("log");
    assert(log && log->type == NodeType::kMap);
    const auto* level = log->find("level");
    assert(level && level->type == NodeType::kScalar && level->scalar == "INFO");
    std::cout << "  [PASS] test_valid_map" << std::endl;
}

void test_sequence() {
    auto n = YamlParser::parse("items:\n  - a\n  - b\n  - c\n", "mem");
    const auto* items = n->find("items");
    assert(items && items->type == NodeType::kSequence);
    assert(items->sequence.size() == 3);
    assert(items->sequence[0]->scalar == "a");
    assert(items->sequence[2]->scalar == "c");
    std::cout << "  [PASS] test_sequence" << std::endl;
}

void test_syntax_error() {
    bool threw = false;
    try {
        YamlParser::parse("common: [unclosed\n", "mem");
    } catch (const ConfigException& e) {
        threw = true;
        assert(e.code == "CGW-FW-0002");
    }
    assert(threw);
    std::cout << "  [PASS] test_syntax_error" << std::endl;
}

void test_empty_file_yields_empty_map() {
    auto n = YamlParser::parse("", "mem");
    assert(n->type == NodeType::kMap);
    assert(n->map.empty());
    std::cout << "  [PASS] test_empty_file_yields_empty_map" << std::endl;
}

void test_explicit_null_value() {
    auto n = YamlParser::parse("common: ~\nlog: null\n", "mem");
    const auto* common = n->find("common");
    assert(common && common->type == NodeType::kNull);
    const auto* log = n->find("log");
    assert(log && log->type == NodeType::kNull);
    std::cout << "  [PASS] test_explicit_null_value" << std::endl;
}

void test_empty_map_and_sequence() {
    auto n = YamlParser::parse("a: {}\nb: []\n", "mem");
    const auto* a = n->find("a");
    assert(a && a->type == NodeType::kMap && a->map.empty());
    const auto* b = n->find("b");
    assert(b && b->type == NodeType::kSequence && b->sequence.empty());
    std::cout << "  [PASS] test_empty_map_and_sequence" << std::endl;
}

void test_integer_scalar() {
    auto n = YamlParser::parse("port: 8080\n", "mem");
    const auto* port = n->find("port");
    assert(port && port->type == NodeType::kScalar && port->scalar == "8080");
    std::cout << "  [PASS] test_integer_scalar" << std::endl;
}

int main() {
    std::cout << "Running YamlParser tests..." << std::endl;
    test_valid_map();
    test_sequence();
    test_syntax_error();
    test_empty_file_yields_empty_map();
    test_explicit_null_value();
    test_empty_map_and_sequence();
    test_integer_scalar();
    std::cout << "All YamlParser tests passed!" << std::endl;
    return 0;
}
