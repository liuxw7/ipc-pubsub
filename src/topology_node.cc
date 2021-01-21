#include <spdlog/spdlog.h>
#include <unistd.h>

#include "TopologyManager.h"

using ipc_pubsub::NodeChange;
using ipc_pubsub::TopicChange;
int main() {
    auto onJoin = [](const NodeChange& msg) { spdlog::info("{}", msg.DebugString()); };
    auto onLeave = [](const NodeChange& msg) { spdlog::info("{}", msg.DebugString()); };
    auto onAnnounce = [](const TopicChange& msg) { spdlog::info("{}", msg.DebugString()); };
    auto onRetract = [](const TopicChange& msg) { spdlog::info("{}", msg.DebugString()); };
    auto onSubscribe = [](const TopicChange& msg) { spdlog::info("{}", msg.DebugString()); };
    auto onUnsubscribe = [](const TopicChange& msg) { spdlog::info("{}", msg.DebugString()); };

    TopologyManager mgr("hello", "node1", onJoin, onLeave, onAnnounce, onRetract, onSubscribe,
                        onUnsubscribe);
    while (true) {
        SPDLOG_INFO("Still alive");
        sleep(1);
    }
}
