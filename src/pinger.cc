#include <chrono>
#include <iostream>
#include <thread>

#include "ips/IPCNode.h"
using namespace ips;

int main() {
    auto node = IPCNode::Create("/pingpong2", "pinger");
    if (node == nullptr) {
        return 1;
    }

    // shouldn't receive our own messages
    node->Subscribe("/ping", [](size_t len, const uint8_t* data) {
        std::cout.write(reinterpret_cast<const char*>(data), len);
        std::cout << "self ping recvd" << std::endl;
    });
    node->Subscribe("/pong", [](size_t len, const uint8_t* data) {
        std::cout.write(reinterpret_cast<const char*>(data), len);
        std::cout << " recvd" << std::endl;
    });
    node->Announce("/ping", "text/plain");
    for (size_t i = 0; i < 2; ++i) {
        std::string msg = "ping";
        node->Publish("/ping", msg.size() + 1, reinterpret_cast<const uint8_t*>(msg.c_str()));
        std::cout << "ping sent" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
