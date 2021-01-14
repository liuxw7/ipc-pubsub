#include <chrono>
#include <iostream>
#include <thread>

#include "IPCMessenger.h"

int main() {
    auto node = IPCMessenger::Create("/pingpong2", "pinger");
    if (node == nullptr) {
        return 1;
    }

    // shouldn't receive our own messages
    node->Subscribe("/ping", [](const uint8_t* data, size_t len) {
        std::cout.write(reinterpret_cast<const char*>(data), len);
        std::cout << "self ping recvd" << std::endl;
    });
    node->Subscribe("/pong", [](const uint8_t* data, size_t len) {
        std::cout.write(reinterpret_cast<const char*>(data), len);
        std::cout << " recvd" << std::endl;
    });
    node->Announce("/ping", "text/plain");
    for (size_t i = 0; i < 2; ++i) {
        std::string msg = "ping";
        node->Publish("/ping", reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size() + 1);
        std::cout << "ping sent" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
