// examples/someip_consumer.cpp - minimal CGW Provider/Client consumer sample
// (CGW-FW-DSN-CR-005 §15.4).
//
// Demonstrates a service acting as both Provider and Client over the controlled
// (fake) backend: offer a method, discover, call and receive the response. The
// framework is not a central daemon; the Provider and Client communicate
// directly (US-504). Links the exported build-tree alias cgw::framework-someip.
// This is a sample, NOT a unit test.
#include "cgw/fw/someip/runtime.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main() {
    using namespace cgw::fw::someip;

    SomeIpConfig cfg;
    cfg.application = "cgw-someip-consumer";
    cfg.callTimeout = std::chrono::milliseconds(1000);
    cfg.discovery.initialBackoff = std::chrono::milliseconds(50);
    cfg.discovery.maxBackoff = std::chrono::milliseconds(500);

    SomeIpRuntime rt = SomeIpRuntime::create(cfg);
    rt.start();

    // A Provider offers Service 0x1110 / Instance 0x0001, InterfaceVersion 1.0.
    Provider provider = rt.createProvider({0x1110, 0x0001}, {1, 0});
    provider.registerMethod(0x0001, [](const RequestContext& ctx, PayloadView pv) {
        std::string in(reinterpret_cast<const char*>(pv.data), pv.size);
        std::string out = "echo:" + in;
        Payload resp(out.begin(), out.end());
        return MethodResult::ok(std::move(resp));
    });
    provider.offer();

    // A Client requests the same service and calls the method directly.
    Client client = rt.createClient({0x1110, 0x0001}, {1, 0});
    client.requestService();
    // The default thread pool delivers availability asynchronously; wait for it
    // before calling (US-503 availability gating).
    for (int i = 0; i < 2000 && client.availability() != Availability::Available; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::string request = "hello";
    Payload req(request.begin(), request.end());
    CallResult result = client.call(0x0001, PayloadView{req.data(), req.size()});

    std::string response(result.response.begin(), result.response.end());
    std::cout << "request:  " << request << "\n"
              << "response: " << response << "\n"
              << "success:  " << (result.success ? "true" : "false") << std::endl;

    if (!result.success || response != "echo:hello") {
        std::cerr << "someip consumer sample FAILED" << std::endl;
        return 1;
    }

    rt.stop();
    std::cout << "consumer sample OK" << std::endl;
    return 0;
}
