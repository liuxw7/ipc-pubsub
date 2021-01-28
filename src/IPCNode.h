#pragma once

namespace google::protobuf {
class MessageLite;
}

class IPCNode {
   public:
    using RawCallback = std::function<void(int64_t len, uint8_t* data)>;
    using ProtoCallback = std::function<void(const MessageLite& msg)>;

    static std::shared_ptr<IPCNode> Create();
    IPCNode();
    void Announce();

    void Publish(std::string_view topic, int64_t len, uint8_t* data);
    void Publish(std::string_view topic, const MessageLite& msg);

    void Subscribe(std::string_view topic, RawCallback);
    void Subscribe(std::string_view topic, ProtoCallback);

   private:
    struct NodeConnection {
        uint64_t nodeId;
        int mFd;  // where to write new messages
    };

    struct Topic {
        RawCallback rawCb = nullptr;
        ProtoCallback protoCb = nullptr;
        std::vector<NodeConnection> mWriters;
    };

    std::unordered_map<std::string, Topic> mTopics;

    std::shared_ptr<TopologyManager> mTopologyManager;

    // where we'll recieve messages (on all topics, from all nodes)
    int mInputFd = -1;

    // Event for shutting down main thread
    int mShutdownFd = -1;

    // Thread that reads from input until shutdown event
    std::thread mReadThread;
};
