#include <spdlog/spdlog.h>

#include <chrono>
#include <iostream>
#include <thread>

#include "ipc_pubsub/IPCNode.h"
using namespace ips;

int main() {
    auto node = IPCNode::Create("/pingpong2", "pinger");
    if (node == nullptr) {
        return 1;
    }
    node->Announce("/ping", "text/plain");

    // shouldn't receive our own messages
    node->Subscribe("/ping", [](ssize_t len, const uint8_t* data) {
        SPDLOG_INFO("Self Ping received, {}", data);
    });

    node->Subscribe(
        "/pong", [](ssize_t len, const uint8_t* data) { SPDLOG_INFO("Pong received, {}", data); });

    for (size_t i = 0; i < 100; ++i) {
        std::string msg = "ping";
        node->Publish("/ping", msg.size() + 1, reinterpret_cast<const uint8_t*>(msg.c_str()));
        std::cout << "ping sent" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
