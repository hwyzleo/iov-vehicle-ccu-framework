#include "log_types.h"
#include "log/log_enricher.h"
#include <cassert>
#include <iostream>
#include <algorithm>

using namespace cgw::fw::log;

static bool hasField(const std::vector<Field>& fields, const std::string& key) {
    return std::find_if(fields.begin(), fields.end(),
        [&key](const Field& f) { return f.key == key; }) != fields.end();
}

static const Field* findField(const std::vector<Field>& fields, const std::string& key) {
    auto it = std::find_if(fields.begin(), fields.end(),
        [&key](const Field& f) { return f.key == key; });
    return (it != fields.end()) ? &(*it) : nullptr;
}

void test_enricher_basic_fields() {
    Enricher enricher("test_svc");
    std::vector<Field> fields;
    fields.push_back({"user_id", FieldValue::makeString("U123")});

    auto enriched = enricher.enrich(fields, LogLevel::kInfo, "uds", "test.event", "Test message");

    assert(hasField(enriched, "schema_version"));
    assert(hasField(enriched, "timestamp"));
    assert(hasField(enriched, "time_synced"));
    assert(hasField(enriched, "mono_ms"));
    assert(hasField(enriched, "level"));
    assert(hasField(enriched, "service"));
    assert(hasField(enriched, "module"));
    assert(hasField(enriched, "event"));
    assert(hasField(enriched, "message"));
    assert(hasField(enriched, "pid"));
    assert(hasField(enriched, "tid"));
    assert(hasField(enriched, "user_id"));

    const Field* svc = findField(enriched, "service");
    assert(svc != nullptr && svc->value.stringVal == "test_svc");

    const Field* lvl = findField(enriched, "level");
    assert(lvl != nullptr && lvl->value.stringVal == "INFO");

    std::cout << "  [PASS] test_enricher_basic_fields" << std::endl;
}

void test_enricher_with_context() {
    Enricher enricher("diag");
    LogContext ctx;
    ctx.trace_id = "trace-abc";
    ctx.request_id = "req-123";
    ctx.session_id = "sess-xyz";

    std::vector<Field> fields;
    auto enriched = enricher.enrich(fields, LogLevel::kError, "uds", "diag.uds.failed", "UDS failed", &ctx);

    assert(hasField(enriched, "trace_id"));
    assert(hasField(enriched, "request_id"));
    assert(hasField(enriched, "session_id"));

    const Field* trace = findField(enriched, "trace_id");
    assert(trace != nullptr && trace->value.stringVal == "trace-abc");

    std::cout << "  [PASS] test_enricher_with_context" << std::endl;
}

void test_enricher_mono_ms_increasing() {
    Enricher enricher("test");
    auto r1 = enricher.enrich({}, LogLevel::kInfo, "m", "e", "msg1");
    for (volatile int i = 0; i < 100000; ++i) {}
    auto r2 = enricher.enrich({}, LogLevel::kInfo, "m", "e", "msg2");

    const Field* mono1 = findField(r1, "mono_ms");
    const Field* mono2 = findField(r2, "mono_ms");
    assert(mono1 != nullptr && mono2 != nullptr);
    assert(mono2->value.intVal >= mono1->value.intVal);

    std::cout << "  [PASS] test_enricher_mono_ms_increasing" << std::endl;
}

int main() {
    std::cout << "Running Enricher tests..." << std::endl;
    test_enricher_basic_fields();
    test_enricher_with_context();
    test_enricher_mono_ms_increasing();
    std::cout << "All Enricher tests passed!" << std::endl;
    return 0;
}
