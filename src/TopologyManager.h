#pragma once
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "protos/index.pb.h"

class TopologyServer;
class TopologyStore;
class UDSServer;
class UDSClient;

class TopologyManager {
   public:
    using NodeChangeHandler = std::function<void(const ipc_pubsub::NodeChange&)>;
    using TopicChangeHandler = std::function<void(const ipc_pubsub::TopicChange&)>;

    TopologyManager(std::string_view announcePath, std::string_view name,
                    NodeChangeHandler onJoin = nullptr, NodeChangeHandler onLeave = nullptr,
                    TopicChangeHandler onAnnounce = nullptr, TopicChangeHandler onRecant = nullptr,
                    TopicChangeHandler onSubscribe = nullptr,
                    TopicChangeHandler onUnsubscribe = nullptr);

    ~TopologyManager();
    void Apply(const ipc_pubsub::TopologyMessage& msg);
    ipc_pubsub::TopologyMessage GetNodeMessage(uint64_t nodeId);
    ipc_pubsub::TopologyMessage GetClientDescriptionMessage(int fd);

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
    std::shared_ptr<UDSClient> CreateClient();
    void ApplyUpdate(const ipc_pubsub::TopologyMessage& msg);

    int mShutdownFd = -1;
    std::atomic_bool mShutdown = false;
    std::string mAnnouncePath;

    std::string mAddress;
    std::string mName;
    uint64_t mNodeId;

    std::thread mMainThread;

    // not necessarily running, but one TopologyManager will create one and
    // if the client drops it will attempt to create a new server
    std::shared_ptr<TopologyServer> mServer;

    // Handles New Topology Updates and can send our entry / exit / publish / subscribe
    // messages
    std::shared_ptr<UDSClient> mClient;

    std::shared_ptr<TopologyStore> mStore;

    // Callbacks
    NodeChangeHandler mOnJoin;
    NodeChangeHandler mOnLeave;
    TopicChangeHandler mOnAnnounce;
    TopicChangeHandler mOnRetract;
    TopicChangeHandler mOnSubscribe;
    TopicChangeHandler mOnUnsubscribe;
};
