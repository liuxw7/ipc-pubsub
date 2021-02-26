#include <ipc_pubsub/Subscriber.h>

#include <iostream>

using ipc_pubsub::IPCMessage;
using ipc_pubsub::Subscriber;
static void Callback(std::shared_ptr<IPCMessage> msg) {
    std::cerr << "Message Received" << std::endl;
    std::cerr << "Meta: " << msg->MetaData() << std::endl;
    std::cerr << "Msg: " << msg->Contents() << std::endl;
}

int main(int argc, char** argv) {
    Subscriber sub(Callback);
    sub.WaitForShutdown();
    return 0;
}
