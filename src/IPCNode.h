#pragma once
#include <functional>
#include <memory>
#include <unordered_map>

#include "ips/TopologyManager.h"
namespace ips {
class IPCNode {
    //   public:
    //    using RawCallback = std::function<void(int64_t len, uint8_t* data)>;
    //
    //    static std::shared_ptr<IPCNode> Create();
    //    IPCNode();
    //    void Announce();
    //    void Publish(std::string_view topic, int64_t len, uint8_t* data);
    //    void Subscribe(std::string_view topic, RawCallback);
    //
   private:
    //    int MainLoop();
    //
    //    struct NodeConnection {
    //        uint64_t nodeId;
    //        int mFd;  // where to write new messages
    //    };
    //
    //    std::mutex mMtx;
    //    std::unordered_map<std::string, RawCallback> mSubscriptions;
    //
    //    std::shared_ptr<TopologyManager> mTopologyManager;
    //
    //    // where we'll recieve messages (on all topics, from all nodes)
    //    int mInputFd = -1;
    //
    //    // Event for shutting down main thread
    //    int mShutdownFd = -1;
    //
    //    // Thread that reads from input until shutdown event
    //    std::thread mReadThread;
};
}  // namespace ips
