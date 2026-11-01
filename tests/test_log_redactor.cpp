#include "log_types.h"
#include "log/log_redactor.h"
#include <cassert>
#include <iostream>
#include <algorithm>

using namespace ccu::fw::log;

static const Field* findField(const std::vector<Field>& fields, const std::string& key) {
    auto it = std::find_if(fields.begin(), fields.end(),
        [&key](const Field& f) { return f.key == key; });
    return (it != fields.end()) ? &(*it) : nullptr;
}

void test_redactor_secret_rejected() {
    RedactConfig cfg;
    Redactor redactor(cfg);

    std::vector<Field> fields;
    fields.push_back({"password", FieldValue::makeString("mysecretpass")});
    fields.push_back({"username", FieldValue::makeString("admin")});

    auto result = redactor.redact(std::move(fields));

    const Field* pass = findField(result, "password_redacted");
    assert(pass != nullptr);
    assert(pass->value.stringVal.find("REDACTED") != std::string::npos);

    const Field* user = findField(result, "username");
    assert(user != nullptr && user->value.stringVal == "admin");

    std::cout << "  [PASS] test_redactor_secret_rejected" << std::endl;
}

void test_redactor_secret_key_names() {
    RedactConfig cfg;
    Redactor redactor(cfg);

    std::vector<std::string> secretKeys = {
        "password", "token", "secret", "private_key", "seed", "key_material"
    };

    for (const auto& key : secretKeys) {
        std::vector<Field> fields;
        fields.push_back({key, FieldValue::makeString("value")});
        auto result = redactor.redact(std::move(fields));
        assert(result.size() == 1);
        assert(result[0].key == key + "_redacted");
    }

    std::cout << "  [PASS] test_redactor_secret_key_names" << std::endl;
}

void test_redactor_identifier_mask() {
    RedactConfig cfg;
    cfg.identifiers = "mask";
    Redactor redactor(cfg);

    std::vector<Field> fields;
    fields.push_back({"vin", FieldValue::makeString("LVSHFFAN5KF000001"), Sensitivity::Identifier});

    auto result = redactor.redact(std::move(fields));
    assert(result.size() == 1);
    assert(result[0].value.stringVal == "LV****01");

    std::cout << "  [PASS] test_redactor_identifier_mask" << std::endl;
}

void test_redactor_payload_truncate() {
    RedactConfig cfg;
    cfg.raw_payload_max_bytes = 10;
    Redactor redactor(cfg);

    std::vector<Field> fields;
    fields.push_back({"raw_data", FieldValue::makeString("01020304050607080910111213"), Sensitivity::Payload});

    auto result = redactor.redact(std::move(fields));
    assert(result.size() == 1);
    assert(result[0].value.stringVal.find("truncated") != std::string::npos);

    std::cout << "  [PASS] test_redactor_payload_truncate" << std::endl;
}

void test_redactor_normal_passthrough() {
    RedactConfig cfg;
    Redactor redactor(cfg);

    std::vector<Field> fields;
    fields.push_back({"event_name", FieldValue::makeString("test.event")});
    fields.push_back({"count", FieldValue::makeInt(42)});

    auto result = redactor.redact(std::move(fields));
    assert(result.size() == 2);
    assert(result[0].value.stringVal == "test.event");
    assert(result[1].value.intVal == 42);

    std::cout << "  [PASS] test_redactor_normal_passthrough" << std::endl;
}

int main() {
    std::cout << "Running Redactor tests..." << std::endl;
    test_redactor_secret_rejected();
    test_redactor_secret_key_names();
    test_redactor_identifier_mask();
    test_redactor_payload_truncate();
    test_redactor_normal_passthrough();
    std::cout << "All Redactor tests passed!" << std::endl;
    return 0;
}
