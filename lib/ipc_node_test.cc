#include <chrono>
#include <thread>

#include "ipc_pubsub/IPCNode.h"
int main() {
    auto node = IPCNode::Create("group");
    std::this_thread::sleep_for(std::chrono::seconds(5));
}
