#include <chrono>
#include <iostream>
#include <thread>

#include "ips/IPCMessenger.h"

int main() {
    auto node = IPCMessenger::Create("/pingpong2", "ponger");
    if (node == nullptr) {
        return 1;
    }

    // shouldn't receive our own messages
    node->Subscribe("/pong", [](const uint8_t* data, size_t len) {
        std::cout.write(reinterpret_cast<const char*>(data), len);
        std::cout << "self pong recvd" << std::endl;
    });

    node->Announce("/pong", "text/plain");
    node->Subscribe("/ping", [&node](const uint8_t* data, uint64_t len) {
        std::cout.write(reinterpret_cast<const char*>(data), len);
        std::cout << " recvd" << std::endl;
        node->Publish("/pong", data, len);
    });
    std::this_thread::sleep_for(std::chrono::seconds(10));
}
