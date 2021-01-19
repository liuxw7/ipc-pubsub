#pragma once
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "protos/index.pb.h"

class TopologyManager {
   public:
    TopologyManager(std::string_view socketPath, std::string_view name);
    ~TopologyManager();
    void Apply(const ipc_pubsub::TopologyMessage& msg);
    ipc_pubsub::TopologyMessage GetNodeMessage(uint64_t nodeId);
    ipc_pubsub::TopologyMessage GetClientDescriptionMessage(int fd);
    void CreateClient(int fd);
    void DeleteClient(int fd);

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
    void MainLoop();
    void RunClient();
    void RunServer();
    void ApplyUpdate(const ipc_pubsub::TopologyMessage& msg);

    int mShutdownFd = -1;
    std::atomic_bool mShutdown = false;
    std::string mSocketPath;

    std::mutex mMtx;
    std::unordered_map<uint64_t, std::shared_ptr<Node>> mNodeById;

    uint64_t mNodeId;

    std::thread mMainThread;
};
