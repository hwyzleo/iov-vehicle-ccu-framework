#include "config/config_merger.h"
#include "config/config_node.h"
#include "config/config_path_resolver.h"
#include <cassert>
#include <iostream>
#include <string>

using namespace ccu::fw::config;

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

void test_map_deep_merge() {
    auto base = map({{"a", scalar("1")}, {"b", map({{"x", scalar("1")}})}});
    auto over = map({{"b", map({{"y", scalar("2")}})}, {"c", scalar("3")}});
    auto m = Merger::merge(base, over);
    assert(m->find("a")->scalar == "1");          // retained
    assert(m->find("c")->scalar == "3");          // new
    const auto* b = m->find("b");
    assert(b->find("x")->scalar == "1");          // deep-merged
    assert(b->find("y")->scalar == "2");
    std::cout << "  [PASS] test_map_deep_merge" << std::endl;
}

void test_scalar_override() {
    auto m = Merger::merge(map({{"a", scalar("1")}}), map({{"a", scalar("2")}}));
    assert(m->find("a")->scalar == "2");
    std::cout << "  [PASS] test_scalar_override" << std::endl;
}

void test_sequence_replace() {
    auto base = map({{"s", seq({scalar("1"), scalar("2")})}});
    auto over = map({{"s", seq({scalar("3")})}});
    auto m = Merger::merge(base, over);
    const auto* s = m->find("s");
    assert(s->sequence.size() == 1);              // replaced, not concatenated
    assert(s->sequence[0]->scalar == "3");
    std::cout << "  [PASS] test_sequence_replace" << std::endl;
}

void test_explicit_null_override() {
    auto m = Merger::merge(map({{"a", scalar("1")}}), map({{"a", nullNode()}}));
    const auto* a = m->find("a");
    assert(a != nullptr);
    assert(a->type == NodeType::kNull);           // explicit null kept, distinct from missing
    std::cout << "  [PASS] test_explicit_null_override" << std::endl;
}

void test_type_conflict_overlay_wins() {
    auto base = map({{"a", map({{"x", scalar("1")}})}});
    auto over = map({{"a", scalar("5")}});
    auto m = Merger::merge(base, over);
    assert(m->find("a")->type == NodeType::kScalar);
    assert(m->find("a")->scalar == "5");
    std::cout << "  [PASS] test_type_conflict_overlay_wins" << std::endl;
}

void test_merge_all_ordering() {
    ConfigEntry e1{}, e2{}, e3{};
    std::vector<std::pair<ConfigEntry, NodePtr>> layers = {
        {e1, map({{"a", scalar("1")}})},
        {e2, map({{"a", scalar("2")}})},
        {e3, map({{"b", scalar("3")}})},
    };
    auto m = Merger::mergeAll(layers);
    assert(m->find("a")->scalar == "2");          // last wins
    assert(m->find("b")->scalar == "3");          // new key from later layer
    std::cout << "  [PASS] test_merge_all_ordering" << std::endl;
}

void test_unoverlapped_keys_retained() {
    auto base = map({{"keep", scalar("yes")}});
    auto over = map({{"add", scalar("ok")}});
    auto m = Merger::merge(base, over);
    assert(m->find("keep")->scalar == "yes");
    assert(m->find("add")->scalar == "ok");
    std::cout << "  [PASS] test_unoverlapped_keys_retained" << std::endl;
}

int main() {
    std::cout << "Running Merger tests..." << std::endl;
    test_map_deep_merge();
    test_scalar_override();
    test_sequence_replace();
    test_explicit_null_override();
    test_type_conflict_overlay_wins();
    test_merge_all_ordering();
    test_unoverlapped_keys_retained();
    std::cout << "All Merger tests passed!" << std::endl;
    return 0;
}
