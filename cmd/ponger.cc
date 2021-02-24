#include <spdlog/spdlog.h>

#include <chrono>
#include <iostream>
#include <thread>

#include "ips/IPCNode.h"

using namespace ips;
int main() {
    auto node = IPCNode::Create("/pingpong2", "ponger");
    if (node == nullptr) {
        return 1;
    }
    node->Announce("/pong", "text/plain");

    // shouldn't receive our own messages
    node->Subscribe("/pong", [](ssize_t len, const uint8_t* data) {
        SPDLOG_INFO("Self Pong received, {}", data);
    });

    node->Subscribe("/ping", [&node](ssize_t len, const uint8_t* data) {
        SPDLOG_INFO("Ping received, {}", data);
        node->Publish("/pong", len, data);
    });

    std::this_thread::sleep_for(std::chrono::seconds(100));
}
