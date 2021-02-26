#include <ipc_pubsub/Publisher.h>

using ipc_pubsub::Publisher;

int main(int argc, char** argv) {
    Publisher publisher;
    std::string msg = "hello";
    std::string meta = "txt";
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        publisher.Send(meta, msg.size() + 1, reinterpret_cast<const uint8_t*>(msg.c_str()));
    }
}
