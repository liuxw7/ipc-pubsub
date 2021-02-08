#pragma once
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "ips/TopologyManager.h"
namespace ips {

struct IPCNeighbor;

class IPCNode {
   public:
    using RawCallback = std::function<void(int64_t len, uint8_t* data)>;

    static std::shared_ptr<IPCNode> Create(const std::string& groupName,
                                           const std::string& nodeName);

    // Don't call directly, call Create()
    IPCNode(int shutdownFd, int listenSock, int outSock, const std::string& groupName,
            const std::string& nodeName, uint64_t nodeId, const std::string& dataPath);
    ~IPCNode();

    void Announce(const std::string& topic, const std::string& mime);
    void Publish(const std::string& topic, int64_t len, const uint8_t* data);
    void Subscribe(const std::string& topic, RawCallback);
    void Unsubscribe(const std::string& topic);
    void Retract(const std::string& topic);

   private:
    void OnData(int64_t len, uint8_t* data);
    int MainLoop();
    void OnJoin(const ips::NodeChange& msg);
    void OnLeave(const ips::NodeChange& msg);
    void OnAnnounce(const ips::TopicChange& msg);
    void OnRetract(const ips::TopicChange& msg);
    void OnSubscribe(const ips::TopicChange& msg);
    void OnUnsubscribe(const ips::TopicChange& msg);

    //
    //    struct NodeConnection {
    //        uint64_t nodeId;
    //        int mFd;  // where to write new messages
    //    };
    //

    std::mutex mMtx;
    std::unordered_map<uint64_t, std::unique_ptr<IPCNeighbor>> mNodeById;

    //    std::unordered_map<std::string, RawCallback> mSubscriptions;
    //
    std::shared_ptr<TopologyManager> mTopologyManager;

    // Event for shutting down main thread
    int mShutdownFd = -1;

    // where we'll recieve messages (on all topics, from all nodes)
    int mInputFd = -1;

    // Outbound socket that we will use with sendto
    int mOutFd = -1;

    const std::string mGroupName;
    const std::string mNodeName;
    const std::string mDataPath;
    //
    //    // Thread that reads from input until shutdown event
    std::thread mMainThread;
};
}  // namespace ips
