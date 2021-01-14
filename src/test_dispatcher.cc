#include <iostream>

#include "Dispatcher.h"
#include "ShmMessage.h"

int main() {
    auto handler = [](const uint8_t* data, size_t len) {
        std::string msg(reinterpret_cast<const char*>(data), len);
        std::cerr << "Received: '" << msg << "'" << std::endl;
    };

    Dispatcher dispatcher;
    dispatcher.SetCallback("/hello", handler);

    std::string helloMessages[] = {"hello", "world"};
    std::string nopeMessages[] = {"nope-hello", "nope-world"};

    for (const auto& msg : helloMessages) {
        auto shmMsg = ShmMessage::Create(msg.size());
        shmMsg->IncBalance();
        std::copy(msg.begin(), msg.end(), shmMsg->Data());
        dispatcher.Push("/hello", shmMsg->Name());
    }

    for (const auto& msg : nopeMessages) {
        auto shmMsg = ShmMessage::Create(msg.size());
        shmMsg->IncBalance();
        std::copy(msg.begin(), msg.end(), shmMsg->Data());
        dispatcher.Push("/nope", shmMsg->Name());
    }
}
