#include "log.h"
#include "log/log_config_adapter.h"
#include <cassert>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

using namespace cgw::fw::log;

void test_logger_init_and_log() {
    LogConfig config = LogConfigAdapter::getDefaultConfig();
    InitResult result = Logger::init("test_svc", config);
    assert(result.error == LogError::kOk);

    Logger logger = Logger::get("test_module");
    logger.info("test.event", "Test message", {
        {"key1", FieldValue::makeString("value1")},
        {"key2", FieldValue::makeInt(42)}
    });

    logger.flush();
    std::cout << "  [PASS] test_logger_init_and_log" << std::endl;
}

void test_logger_level_filtering() {
    LogConfig config;
    config.level = LogLevel::kWarn;
    config.console_config.enabled = true;
    config.async_config.enabled = false;

    InitResult result = Logger::init("test_level", config);
    assert(result.error == LogError::kOk);

    Logger logger = Logger::get("test");

    logger.trace("t.e", "should not appear");
    logger.debug("t.e", "should not appear");
    logger.info("t.e", "should not appear");
    logger.warn("t.e", "should appear");
    logger.error("t.e", "should appear");

    logger.flush();
    std::cout << "  [PASS] test_logger_level_filtering" << std::endl;
}

void test_logger_context_propagation() {
    LogConfig config = LogConfigAdapter::getDefaultConfig();
    config.async_config.enabled = false;

    Logger::init("test_ctx", config);
    Logger logger = Logger::get("uds");

    LogContext ctx;
    ctx.trace_id = "trace-integration";
    ctx.request_id = "req-integration";

    {
        ContextScope scope(ctx);
        logger.info("diag.uds.request", "UDS request started", {
            {"did", FieldValue::makeString("0x10")}
        });
    }

    logger.flush();
    std::cout << "  [PASS] test_logger_context_propagation" << std::endl;
}

void test_logger_redaction() {
    LogConfig config = LogConfigAdapter::getDefaultConfig();
    config.async_config.enabled = false;

    Logger::init("test_redact", config);
    Logger logger = Logger::get("sec");

    logger.info("sec.auth.login", "User login", {
        {"username", FieldValue::makeString("admin")},
        {"password", FieldValue::makeString("secret123")},
        {"token", FieldValue::makeString("abc123")}
    });

    logger.flush();
    std::cout << "  [PASS] test_logger_redaction" << std::endl;
}

void test_logger_fatal_aborts() {
    LogConfig config = LogConfigAdapter::getDefaultConfig();
    config.async_config.enabled = false;

    Logger::init("test_fatal", config);
    Logger logger = Logger::get("test");

    pid_t pid = fork();
    if (pid == 0) {
        logger.fatal("test.fatal", "This should abort");
        _exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
        std::cout << "  [PASS] test_logger_fatal_aborts" << std::endl;
    }
}

int main() {
    std::cout << "Running integration tests..." << std::endl;
    test_logger_init_and_log();
    test_logger_level_filtering();
    test_logger_context_propagation();
    test_logger_redaction();
    test_logger_fatal_aborts();
    std::cout << "All integration tests passed!" << std::endl;
    return 0;
}
