#include <spdlog/spdlog.h>
#include <unistd.h>

#include "TopologyManager.h"
#include "Utils.h"

using ipc_pubsub::NodeChange;
using ipc_pubsub::TopicChange;
int main() {
    auto onJoin = [](const NodeChange& msg) { spdlog::info("{}", msg.DebugString()); };
    auto onLeave = [](const NodeChange& msg) { spdlog::info("{}", msg.DebugString()); };
    auto onAnnounce = [](const TopicChange& msg) { spdlog::info("{}", msg.DebugString()); };
    auto onRetract = [](const TopicChange& msg) { spdlog::info("{}", msg.DebugString()); };
    auto onSubscribe = [](const TopicChange& msg) { spdlog::info("{}", msg.DebugString()); };
    auto onUnsubscribe = [](const TopicChange& msg) { spdlog::info("{}", msg.DebugString()); };

    std::string nodeName = GenRandom(8);
    std::string nodeData = GenRandom(8);
    uint64_t nodeId = GenRandom();
    TopologyManager mgr("hello", nodeName, nodeId, nodeData, onJoin, onLeave, onAnnounce, onRetract,
                        onSubscribe, onUnsubscribe);
    while (true) {
        SPDLOG_INFO("Still alive");
        sleep(1);
    }
}
