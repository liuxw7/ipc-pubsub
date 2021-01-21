#pragma once
#include <spdlog/spdlog.h>

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "protos/index.pb.h"

class TopologyStore {
   public:
    // Updates the state based on the contents of the passed message and returns a
    // new message with *only* the effective changes, so if nodes join that
    // already existed they won't be in the output message
    ipc_pubsub::TopologyMessage ApplyUpdate(const ipc_pubsub::TopologyMessage& msg);
    ipc_pubsub::TopologyMessage GetNodeMessage(uint64_t nodeId);

    struct Publication {
        std::string name;
        std::string mime;
    };
    struct Node {
        uint64_t id;
        std::string name;
        std::string address;
        std::unordered_map<std::string, Publication> publications;
        std::unordered_set<std::string> subscriptions;
    };

   private:
    std::mutex mMtx;
    std::unordered_map<uint64_t, std::shared_ptr<Node>> mNodeById;
};
