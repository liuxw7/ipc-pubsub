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

    // shouldn't receive our own messages
    node->Subscribe("/pong", [](size_t len, const uint8_t* data) {
        std::cout.write(reinterpret_cast<const char*>(data), len);
        std::cout << "self pong recvd" << std::endl;
    });

    node->Announce("/pong", "text/plain");
    node->Subscribe("/ping", [&node](size_t len, const uint8_t* data) {
        std::cout.write(reinterpret_cast<const char*>(data), len);
        std::cout << " recvd" << std::endl;
        node->Publish("/pong", len, data);
    });
    std::this_thread::sleep_for(std::chrono::seconds(100));
}
