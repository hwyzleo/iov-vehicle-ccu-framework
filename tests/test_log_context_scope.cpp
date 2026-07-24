#include "log_types.h"
#include "log.h"
#include <cassert>
#include <iostream>
#include <thread>

using namespace cgw::fw::log;

void test_context_scope_basic() {
    assert(ContextScope::current() == nullptr);

    {
        LogContext ctx;
        ctx.trace_id = "trace-1";
        ctx.request_id = "req-1";
        ContextScope scope(std::move(ctx));

        const LogContext* current = ContextScope::current();
        assert(current != nullptr);
        assert(current->trace_id == "trace-1");
        assert(current->request_id == "req-1");
    }

    assert(ContextScope::current() == nullptr);

    std::cout << "  [PASS] test_context_scope_basic" << std::endl;
}

void test_context_scope_nested() {
    {
        LogContext outer;
        outer.trace_id = "outer-trace";
        ContextScope outerScope(std::move(outer));

        assert(ContextScope::current()->trace_id == "outer-trace");

        {
            LogContext inner;
            inner.trace_id = "inner-trace";
            inner.request_id = "inner-req";
            ContextScope innerScope(std::move(inner));

            assert(ContextScope::current()->trace_id == "inner-trace");
            assert(ContextScope::current()->request_id == "inner-req");
        }

        assert(ContextScope::current()->trace_id == "outer-trace");
    }

    assert(ContextScope::current() == nullptr);

    std::cout << "  [PASS] test_context_scope_nested" << std::endl;
}

void test_context_scope_thread_isolation() {
    LogContext mainCtx;
    mainCtx.trace_id = "main-trace";
    ContextScope mainScope(std::move(mainCtx));

    std::thread child([]() {
        assert(ContextScope::current() == nullptr);

        LogContext childCtx;
        childCtx.trace_id = "child-trace";
        ContextScope childScope(std::move(childCtx));

        assert(ContextScope::current()->trace_id == "child-trace");
    });
    child.join();

    assert(ContextScope::current()->trace_id == "main-trace");

    std::cout << "  [PASS] test_context_scope_thread_isolation" << std::endl;
}

int main() {
    std::cout << "Running ContextScope tests..." << std::endl;
    test_context_scope_basic();
    test_context_scope_nested();
    test_context_scope_thread_isolation();
    std::cout << "All ContextScope tests passed!" << std::endl;
    return 0;
}
