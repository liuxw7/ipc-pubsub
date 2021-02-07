#include <spdlog/spdlog.h>
#include <unistd.h>

#include "ips/TopologyManager.h"
#include "ips/Utils.h"

using ips::NodeChange;
using ips::TopicChange;
using ips::TopologyManager;
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
