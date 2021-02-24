#pragma once
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "protos/index.pb.h"

namespace ips {
class TopologyServer;
class UDSServer;
class UDSClient;

class TopologyManager {
   public:
    using NodeChangeHandler = std::function<void(const ips::NodeChange&)>;
    using TopicChangeHandler = std::function<void(const ips::TopicChange&)>;

    TopologyManager(const std::string& groupName, const std::string& nodeName, uint64_t nodeId,
                    const std::string& dataPath, NodeChangeHandler onJoin = nullptr,
                    NodeChangeHandler onLeave = nullptr, TopicChangeHandler onAnnounce = nullptr,
                    TopicChangeHandler onRecant = nullptr, TopicChangeHandler onSubscribe = nullptr,
                    TopicChangeHandler onUnsubscribe = nullptr);

    ~TopologyManager();
    void Apply(const ips::TopologyMessage& msg);
    ips::TopologyMessage GetNodeMessage(uint64_t nodeId);
    ips::TopologyMessage GetClientDescriptionMessage(int fd);
    void Announce(const std::string& topic, const std::string& mime);
    void Retract(const std::string& topic);
    void Subscribe(const std::string& topic);
    void Unsubscribe(const std::string& topic);

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
    void ApplyUpdate(const ips::TopologyMessage& msg);
    void ApplyUpdate(uint64_t len, uint8_t* data);
    void IntroduceOurselves(std::shared_ptr<UDSClient>);
    void Send(const ips::TopologyMessage& msg);

    // TODO server could prune nodes from history once they leave and everyone is
    // up-to-date, but the server would have to send a prune notification otherwise
    // it wouldn't have a lasting effect.
    // Clients are probably free to prune immediately
    std::mutex mMtx;
    std::atomic_bool mShutdown = false;
    std::vector<ips::TopologyMessage> mHistory;

    // if for some reason we failed to send a message put the message here
    std::vector<TopologyMessage> mBacklog;

    const uint64_t mNodeId;
    const std::string mAnnouncePath;
    const std::string mAddress;
    const std::string mGroupName;
    const std::string mName;

    std::unordered_map<uint64_t, Node> mNodes;

    std::thread mMainThread;

    std::shared_ptr<UDSClient> mClient;

    // Callbacks
    const NodeChangeHandler mOnJoin;
    const NodeChangeHandler mOnLeave;
    const TopicChangeHandler mOnAnnounce;
    const TopicChangeHandler mOnRetract;
    const TopicChangeHandler mOnSubscribe;
    const TopicChangeHandler mOnUnsubscribe;
};
}  // namespace ips
