#pragma once
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "protos/index.pb.h"

class TopologyServer;
class UDSServer;
class UDSClient;

class TopologyManager {
   public:
    using NodeChangeHandler = std::function<void(const ipc_pubsub::NodeChange&)>;
    using TopicChangeHandler = std::function<void(const ipc_pubsub::TopicChange&)>;

    TopologyManager(std::string_view groupName, std::string_view nodeName, uint64_t nodeId,
                    std::string_view dataPath, NodeChangeHandler onJoin = nullptr,
                    NodeChangeHandler onLeave = nullptr, TopicChangeHandler onAnnounce = nullptr,
                    TopicChangeHandler onRecant = nullptr, TopicChangeHandler onSubscribe = nullptr,
                    TopicChangeHandler onUnsubscribe = nullptr);

    void Shutdown();
    ~TopologyManager();
    void Apply(const ipc_pubsub::TopologyMessage& msg);
    ipc_pubsub::TopologyMessage GetNodeMessage(uint64_t nodeId);
    ipc_pubsub::TopologyMessage GetClientDescriptionMessage(int fd);
    void Announce(std::string_view topic, std::string_view mime);
    void Retract(std::string_view topic);
    void Subscribe(std::string_view topic);
    void Unsubscribe(std::string_view topic);

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
    void SetNewClient(std::shared_ptr<UDSClient>);
    std::shared_ptr<UDSClient> CreateClient();
    void ApplyUpdate(const ipc_pubsub::TopologyMessage& msg);
    void ApplyUpdate(uint64_t len, uint8_t* data);
    void IntroduceOurselves(std::shared_ptr<UDSClient>);

    // TODO server could prune nodes from history once they leave and everyone is
    // up-to-date, but the server would have to send a prune notification otherwise
    // it wouldn't have a lasting effect.
    // Clients are probably free to prune immediately
    std::mutex mMtx;
    std::atomic_bool mShutdown = false;
    std::vector<ipc_pubsub::TopologyMessage> mHistory;

    const uint64_t mNodeId;
    const std::string mAnnouncePath;
    const std::string mAddress;
    const std::string mGroupName;
    const std::string mName;

    std::unordered_map<uint64_t, Node> mNodes;

    std::thread mMainThread;

    // Callbacks
    const NodeChangeHandler mOnJoin;
    const NodeChangeHandler mOnLeave;
    const TopicChangeHandler mOnAnnounce;
    const TopicChangeHandler mOnRetract;
    const TopicChangeHandler mOnSubscribe;
    const TopicChangeHandler mOnUnsubscribe;
};
