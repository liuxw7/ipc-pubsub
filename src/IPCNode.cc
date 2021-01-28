#include "IPCNode.h"

#include <google/protobuf/message_lite.h>
#include <poll.h>

void IPCNode::Publish(std::string_view topic, int64_t len, uint8_t* data) {
    thread_local std::vector<int> fds;
    {
        std::scoped_lock lk(mMtx);
        fds = mFdsByTopic[topic];
    }
    for (const int fd : fds) write(fd, data, len);
}

void IPCNode::Publish(std::string_view topic, const MessageLite& msg) {
    thread_local std::vector<int> fds;
    {
        std::scoped_lock lk(mMtx);
        fds = mFdsByTopic[topic];
    }
    for (const int fd : fds) msg.SerializeToFileDescriptor(fd);
}

void IPCNode::Unsubscribe(std::string_view topic) {
    // publish that we want the messages
    mTopologyManager->Unsubscribe(topic);

    // Remove callbacks
    auto& topicObject = mTopics[topic];
    topicObject.rawCb = nullptr;
    topicObject.protoCb = nullptr;
}

void IPCNode::Subscribe(std::string_view topic, RawCallback cb) {
    // publish that we want the messages
    mTopologyManager->Subscribe(topic);

    // add callback
    auto& topicObject = mTopics[topic];
    topicObject.rawCb = cb;
}

void IPCNode::Subscribe(std::string_view topic, ProtoCallback cb) {
    // publish that we want the messages
    mTopologyManager->Subscribe(topic);

    // add callback
    auto& topicObject = mTopics[topic];
    topicObject.protoCb = cb;
}

void IPCNode::Announce(std::string_view topic, std::string_view mime) {
    mTopologyManager->Announce(topic, mime);
}

void IPCNode::Retract(std::string_view topic) { mTopologyManager->Retract(topic, mime); }

void IPCNode::OnJoin() {}
void IPCNode::OnLeave() {}
void IPCNode::OnAnnounce() {}
void IPCNode::OnRetract() {}
void IPCNode::OnSubscribe() {}
void IPCNode::OnUnsubscribe() {}

void IPCNode::Create(std::string_view groupName, std::string_view nodeName) {
    std::random_device rd;
    std::mt19937_64 e2(rd());
    nodeId = e2();

    // add ourselves to the list of nodes
    std::ostringstream oss;
    oss << '\0' << std::hex << std::setw(16) << std::setfill('0') << nodeId;
    dataPath = oss.str();

    // create socket to read from
    int sock;
    struct sockaddr_un name;

    /* Create socket from which to read. */
    sock = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("opening datagram socket");
        return nullptr;
    }

    /* Create name. */
    name.sun_family = AF_UNIX;
    std::copy(dataPath.begin(), dataPath.end(), name.sun_path);
    name.sun_path[dataPath.size()] = 0;

    /* Bind the UNIX domain address to the created socket */
    if (bind(sock, reinterpret_cast<struct sockaddr*>(&name), sizeof(struct sockaddr_un))) {
        perror("binding name to datagram socket");
        exit(1);
    }
}

void IPCNode::IPCNode(std::string_view groupName, std::string_view nodeName, uint64_t nodeId,
                      std::string_view dataPath)
    : mGroupName(groupName), mNodeName(nodeName), mNodeId(nodeId), mDataPath(dataPath) {
    NodeChangeHandler onJoin = nullptr, auto onJoin = [this](const NodeChange&msg) { OnJoin(msg); };
    auto onLeave = [this](const NodeChange& msg) { OnLeave(msg); };
    auto onAnnounce = [this](const TopicChange& msg) { OnAnnounce(msg); };
    auto onRetract = [this](const TopicChange& msg) { OnRetract(msg); };
    auto onSubscribe = [this](const TopicChange& msg) { OnSubscribe(msg); };
    auto onUnsubscribe = [this](const TopicChange& msg) { OnUnsubscribe(msg); };

    auto topologyManager = std::make_shared<TopologyManager>(nodeId, groupName, dataPath, nodeName,
                                                             callbacks, onJoin, onLeave, onAnnounce,
                                                             onRetract, onSubscribe, onUnsubscribe);

    mMainThread = std::thread([this]() { MainLoop(); });
}

void IPCNode::OnData(int64_t len, uint8_t* data) {
    static thread_local MetadataMessage msg;
    if (!msg.ParseFromArray(len, data)) {
        SPDLOG_ERROR("Failed to parse data of size {}", len);
        return;
    }

    // TODO(micah) get data out of shared memory
    if (!msg.inlined.empty()) {
        rawCb(msg.size(), msg.data());
    }

    RawCallback rawCb;
    ProtoCallback protoCb;
    {
        std::scoped_lock lk(mMtx);
        auto it = mTopics->find(msg.topic);
        if (it == mTopics.end()) return;
    }
}

int IPCNode::MainLoop() {
    // Read from data loop
    struct pollfd fds[2];
    fds[0].fd = mShutdownFd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    fds[1].fd = mInputFd;
    fds[1].events = POLLIN;
    fds[1].revents = 0;
    // now that we are connected field events from leader OR shutdown event
    // wait for it to close or shutdown event
    while (1) {
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            SPDLOG_ERROR("Failed to Poll: {}", strerror(errno));
            return -1;
        }

        if (fds[0].revents != 0) {
            SPDLOG_INFO("Polled shutdown");
            // shutdown event received, exit
            return 0;
        }

        if (fds[1].revents != 0) {
            if (fds[1].revents & POLLERR) {
                SPDLOG_ERROR("poll error");
                return -1;
            } else if (fds[1].revents & POLLNVAL) {
                SPDLOG_INFO("File descriptor not open");
                return -1;
            } else if (fds[1].revents & POLLIN) {
                // socket has data, read it
                uint8_t buffer[UINT16_MAX];
                SPDLOG_INFO("onData");
                int64_t nBytes = read(mFd, buffer, UINT16_MAX);
                if (nBytes < 0) {
                    SPDLOG_ERROR("Error reading: {}", strerror(errno));
                } else {
                    OnData(nBytes, buffer);
                }
            }
        }
    }
}

void IPCNode::~IPCNode() { close(sock); }
