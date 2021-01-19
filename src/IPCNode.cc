#include "IPCNode.h"

#include <poll.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <string>
#include <thread>

#include "UDSClient.h"
#include "UDSServer.h"
#include "protos/index.pb.h"

using ipc_pubsub::NodeOperation;
using ipc_pubsub::TopicOperation;
using ipc_pubsub::TopologyMessage;
constexpr size_t MAX_NOTIFY_SIZE = 2048;
// Managers a set of unix domain socket servers and clients.

// Leader Thread
// construct socket
// while true
//    select shutdown pipe or accept
//    if shutdown
//      for each server thread
//          send byte to shutdown pipe
//      quit
//    if accept()
//       create shutdown pipe pipe
//       create server thread (that calls accept() )
//

/*
 *
 */
class TopologyManager {
   public:
    TopologyManager();
    void Apply(const TopologyMessage& msg);
    TopologyMessage GetCompleteTopology();

   private:
    struct Subscription {
        std::string name;
    };
    struct Publication {
        std::string name;
        std::string mime;
    };
    struct Node {
        uint64_t id;
        std::string name;
        std::string address;
        std::vector<Publication> publications;
        std::vector<Subscription> subscriptions;

        // UDS DRGAM socket to send data messages to
        int dataFd = -1;

        // Only filled out if the leader thread is active,
        // this is the active SOCK_SEQPACKET socket that we need to send / receive new nodes to
        int metaFd = -1;
    };
    std::std::unordered_map<int, std::shared_ptr<Node>> mNodeByFd;
    std::std::unordered_map<uint64_t, std::shared_ptr<Node>> mNodeById;

    std::thread mMainThread;
};

TopologyManager::TopologyManager() {
    mEventFd = eventfd(0, EFD_SEMAPHORE);
    mMainThread([](this) { MainLoop(); });
}

TopologyManager::Shutdown() {
    // very large number so everything receives and decremenets but not UINT64_MAX so we don't roll
    // over
    size_t shutItDown = UINT32_MAX;
    write(mEventFd, &shutItDown, sizeof(shutItDown);
    mShutdown = true;
    mMainThread.join();
}

TopologyManager::ApplyUpdate(int fd, size_t len, uint8_t* data) {
    msg.Parse(data, len);
    ApplyUpdate(fd, msg);
}

TopologyManager::ApplyUpdate(size_t len, uint8_t* data) {
    msg.Parse(data, len);
    ApplyUpdate(msg);
}

TopologyManager::ApplyUpdate(const TopologyMessage& msg) {
    // TODO
}

TopologyManager::ApplyUpdate(int fd, const TopologyMessage& msg) {
    // TODO
}

TopologyMessage::CreateClient(int metaFd) {}

TopologyMessage::DeleteClient(int metaFd) {}

TopologyMessage TopologyManager::GetConnectedStateMessage() {
    // Returns all the clients that are either us (matches mNodeId) or are connected (have a meta
    // fd)
    TopologyMessage out;
    for (const auto& node : mNodeByFd) {
        auto nodeChange = out.mutable_node_changes()->Add();
        nodeChange->set_id(node.id);
        nodeChange->set_name(node.name);
        nodeChange->set_address(node.address);
        nodeChange->set_op(NodeOperation::JOIN);
        for (const auto& sub : node.subscriptions) {
            auto topicChange = out.mutable_topic_changes()->Add();
            topicChange->set_name(sub.name);
            topicChange->set_node_id(node.id);
            topicChange->set_op(TopicOperation::SUBSCRIBE);
        }

        for (const auto& pub : node.publications) {
            auto topicChange = out.mutable_topic_changes()->Add();
            topicChange->set_name(pub.name);
            topicChange->set_mime(pub.mime);
            topicChange->set_node_id(node.id);
            topicChange->set_op(TopicOperation::ANNOUNCE);
        }
    }
    return out;
}

TopologyManager::MainLoop() {
    while (!mShutdown) {
        auto client = UDSClient::Create(mSocketPath);
        if (client != nullptr) {
            // connected send our details
            msg.SerializeToString(&data);
            client->Send(data.size(), data.data());

            client->LoopToShutdownOrClose(
                mShutdownFd, [this]() {}, [this]() { ApplyUpdate(len, data); });

            // disconnected
        }

        // if we exited the client then there might not be a server, try to create one
        auto server = UDSBroadcastServer::Create(mSocketPath);
        if (server != nullptr) {
            // server started,
            // we should immediately get connections followed by node registration
            // NOTE: for the sake of continuity we won't delete the existing nodes
            // until we readd them here, BUT GetConnectedStateMessage() won't
            // include nodes that aren't reconnected yet. This means new connections
            // won't be told about clients that used to be connected to the old
            // leader but haven't connected to use *until* they connect to us
            // and identity themselves.
            server->LoopToShutdownOrClose(
                mShutdownFd,
                [this](int fd) {
                    // new client, send complete state message including all
                    // nodes that we have a connection to (and none that hard ??
                    // because they are disconnected but used to exist)
                    CreateClient(fd);
                    TopologyMessage msg = GetConnectedStateMessage();
                    std::string data;
                    msg.SerializeToString(&data);
                    server->Send(fd, data.size(), data.data());
                },
                [this](int fd) {
                    // notifiy all clients that a client has been removed
                    TopologyMessage msg = DeleteClient(fd);
                    std::string data;
                    msg.SerializeToString(&data);
                    server->Broadcast(data.size(), data.data());
                },
                [this](int fd, size_t len, uint8_t* data) {
                    // notify all clients that a new client has been updated
                    ApplyUpdate(fd, len, data);
                    server->Broadcast(len, data);
                });
        }

        usleep(rand() % 1000);
    }
}

// IPCNode::IPCNode(std::string_view fname, int shutdownFd, int noLeaderFd)
//    : mSocketName(fname),
//      mShutdownFd(shutdownFd),
//      mNoLeaderFd(noLeaderFd),
//      mTopologyManager(new TopologyManager) {
//    mLeaderThread = std::thread([this]() { LeaderLoop(mShutdownFd, mNoLeaderFd); });
//    mClientThread = std::thread(
//        [this]() { ClientLoop(mSocketName, mShutdownFd, mNoLeaderFd, mTopologyManager); });
//}
//
//~IPCNode::IPCNode() {
//    // notify all threads it is time to shut down
//    uint64_t tmp = 1;
//    write(mShutdownFd, &tmp, sizeof(tmp));
//
//    mLeaderThread.join();
//    mClientThread.join();
//    // close(mSock);
//
//    // TODO(micah) unlink if we are the leader and there are no clients?
//    // unlink(NAME.c_str());
//}
////
//// Publish
//// create shared memory
//// unlink it
//// for each reader on topic:
////    send file descriptor to each client
////
//// shutdown
//// connect to leader, send empty message then disconnect (spuriously unblocking leader and
//// server threads)
////
// void IPCNode::Publish(std::string_view topic, size_t len, uint8_t* data) {}
