#include "TopologyManager.h"

#include <poll.h>
#include <spdlog/spdlog.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <functional>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
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

TopologyManager::TopologyManager(std::string_view announcePath, std::string_view name) {
    std::random_device rd;
    std::mt19937_64 e2(rd());
    mNodeId = e2();

    SPDLOG_INFO("Creating {}:{}", name, mNodeId);
    // add ourselves to the list of nodes
    auto node = std::make_shared<Node>();
    node->id = mNodeId;
    node->name = name;
    std::ostringstream oss;
    oss << '\0' << std::hex << std::setw(16) << std::setfill('0') << mNodeId;
    node->address = oss.str();

    mNodeById[mNodeId] = node;

    // Announce path should be hidden ('\0' start)
    mAnnouncePath.resize(announcePath.size() + 1);
    mAnnouncePath[0] = '\0';
    std::copy(announcePath.begin(), announcePath.end(), mAnnouncePath.begin() + 1);

    mShutdownFd = eventfd(0, EFD_SEMAPHORE);
    mMainThread = std::thread([this]() { MainLoop(); });
}

TopologyManager::~TopologyManager() {
    // very large number so everything receives and decremenets but not UINT64_MAX so we don't roll
    // over
    size_t shutItDown = UINT32_MAX;
    write(mShutdownFd, &shutItDown, sizeof(shutItDown));
    mShutdown = true;
    mMainThread.join();
}

// void TopologyManager::ApplyUpdate(int fd, size_t len, uint8_t* data) {
//    TopologyMessage msg;
//    msg.ParseFromArray(data, len);
//    ApplyUpdate(fd, msg);
//}
//
// TopologyManager::ApplyUpdate(size_t len, uint8_t* data) {
//    msg.Parse(data, len);
//    ApplyUpdate(msg);
//}
//
// TopologyManager::ApplyUpdate(const TopologyMessage& msg) {
//    // TODO
//}
//
// TopologyManager::ApplyUpdate(int fd, const TopologyMessage& msg) {
//    // TODO
//}
//
// TopologyMessage::CreateClient(int metaFd) {}
//
// TopologyMessage::DeleteClient(int metaFd) {}
//
// TopologyMessage TopologyManager::GetConnectedStateMessage() {
//    // Returns all the clients that are either us (matches mNodeId) or are connected (have a meta
//    // fd)
//    TopologyMessage out;
//    for (const auto& node : mNodeByFd) {
//        auto nodeChange = out.mutable_node_changes()->Add();
//        nodeChange->set_id(node.id);
//        nodeChange->set_name(node.name);
//        nodeChange->set_address(node.address);
//        nodeChange->set_op(NodeOperation::JOIN);
//        for (const auto& sub : node.subscriptions) {
//            auto topicChange = out.mutable_topic_changes()->Add();
//            topicChange->set_name(sub.name);
//            topicChange->set_node_id(node.id);
//            topicChange->set_op(TopicOperation::SUBSCRIBE);
//        }
//
//        for (const auto& pub : node.publications) {
//            auto topicChange = out.mutable_topic_changes()->Add();
//            topicChange->set_name(pub.name);
//            topicChange->set_mime(pub.mime);
//            topicChange->set_node_id(node.id);
//            topicChange->set_op(TopicOperation::ANNOUNCE);
//        }
//    }
//    return out;
//}

static TopologyMessage SerializeNode(const TopologyManager::Node& node) {
    TopologyMessage out;
    auto nodeChange = out.mutable_node_changes()->Add();
    nodeChange->set_id(node.id);
    nodeChange->set_name(node.name);
    nodeChange->set_address(node.address);
    nodeChange->set_op(NodeOperation::JOIN);
    for (const auto& sub : node.subscriptions) {
        auto topicChange = out.mutable_topic_changes()->Add();
        topicChange->set_name(sub);
        topicChange->set_node_id(node.id);
        topicChange->set_op(TopicOperation::SUBSCRIBE);
    }

    for (const auto& pub : node.publications) {
        auto topicChange = out.mutable_topic_changes()->Add();
        topicChange->set_name(pub.second.name);
        topicChange->set_mime(pub.second.mime);
        topicChange->set_node_id(node.id);
        topicChange->set_op(TopicOperation::ANNOUNCE);
    }

    return out;
}

TopologyMessage TopologyManager::GetNodeMessage(uint64_t nodeId) {
    std::lock_guard<std::mutex> lk(mMtx);
    auto nodePtr = mNodeById.find(nodeId);
    return SerializeNode(*nodePtr->second);
}

void TopologyManager::ApplyUpdate(const TopologyMessage& msg) {
    SPDLOG_INFO("ApplyUpdate: {}", msg.DebugString());
    std::lock_guard<std::mutex> lk(mMtx);
    for (const auto& nodeChange : msg.node_changes()) {
        if (nodeChange.op() == ipc_pubsub::JOIN) {
            auto [it, inserted] = mNodeById.emplace(nodeChange.id(), nullptr);
            if (inserted) {
                it->second = std::make_shared<Node>();
                it->second->id = nodeChange.id();
                it->second->name = nodeChange.name();
                it->second->address = nodeChange.address();
            }
        } else if (nodeChange.op() == ipc_pubsub::LEAVE) {
            auto it = mNodeById.find(nodeChange.id());
            mNodeById.erase(it);
        }
    }
    for (const auto& topicChange : msg.topic_changes()) {
        auto [nit, nodeInserted] = mNodeById.emplace(topicChange.node_id(), nullptr);
        if (nodeInserted) {
            nit->second = std::make_shared<Node>();
            nit->second->id = topicChange.node_id();
        }

        if (topicChange.op() == ipc_pubsub::ANNOUNCE) {
            nit->second->publications.emplace(
                topicChange.name(),
                Publication{.name = topicChange.name(), .mime = topicChange.mime()});
            // TODO validate that the MIME doesn't change?
        } else if (topicChange.op() == ipc_pubsub::SUBSCRIBE) {
            nit->second->subscriptions.emplace(topicChange.name());
        } else if (topicChange.op() == ipc_pubsub::UNSUBSCRIBE) {
            nit->second->subscriptions.erase(topicChange.name());
        }
    }
}

void TopologyManager::RunClient() {
    SPDLOG_INFO("RunClient: {}", mAnnouncePath);
    auto client = UDSClient::Create(mAnnouncePath);
    if (client == nullptr) return;

    // client connected send our details
    client->Send(GetNodeMessage(mNodeId));

    // run for a while
    client->LoopUntilShutdown(mShutdownFd, [this](size_t len, uint8_t* data) {
        SPDLOG_INFO("{} bytes recieved by client");
        TopologyMessage msg;
        msg.ParseFromArray(data, int(len));
        ApplyUpdate(msg);
    });

    // disconnected
}

void TopologyManager::RunServer() {
    SPDLOG_INFO("RunServer: {}", mAnnouncePath);
    // TODO should probably just make a TopologyServer type
    // If we are the leader then we need to keep track of which clients go with which nodes
    std::unordered_map<int, std::shared_ptr<Node>> mNodeByFd;
    auto server = UDSServer::Create(mAnnouncePath);
    if (server == nullptr) return;

    std::mutex serverMtx;
    std::unordered_map<int, uint64_t> fdToNode;
    auto onConnect = [this, &serverMtx, &fdToNode, &server](int fd) {
        // new client, send complete state message including all
        // nodes that we have a connection to (and none that hard ??
        // because they are disconnected but used to exist)
        TopologyMessage msg;
        {
            std::lock_guard<std::mutex> lk(serverMtx);
            for (const auto& pair : fdToNode) {
                msg.MergeFrom(GetNodeMessage(pair.second));
            }
        }

        SPDLOG_INFO("onConnect, sending message");
        server->Send(fd, msg);
    };
    auto onDisconnect = [this, &serverMtx, &fdToNode, &server](int fd) {
        TopologyMessage msg;
        {
            std::lock_guard<std::mutex> lk(serverMtx);
            auto it = fdToNode.find(fd);
            if (it == fdToNode.end()) {
                SPDLOG_ERROR("Node connected but never identified itself");
                return;
            }
            auto nodeChangePtr = msg.mutable_node_changes()->Add();
            nodeChangePtr->set_id(it->second);
            nodeChangePtr->set_op(NodeOperation::LEAVE);

            // notifiy all clients that a client has been removed
            fdToNode.erase(it);
        }
        ApplyUpdate(msg);

        SPDLOG_INFO("broadcasting disconnect message");
        server->Broadcast(msg);
    };
    auto onMessage = [this, &serverMtx, &fdToNode, &server](int fd, size_t len, uint8_t* data) {
        // notify all clients that a new client has been updated
        TopologyMessage msg;
        msg.ParseFromArray(data, int(len));

        // get an ID out of the message (should only have one, clients should only
        // send information about themselves)
        uint64_t id = UINT64_MAX;
        for (const auto& nodeChange : msg.node_changes()) {
            if (id == UINT64_MAX)
                id = nodeChange.id();
            else
                assert(id == nodeChange.id());
        }
        for (const auto& topicChange : msg.topic_changes()) {
            if (id == UINT64_MAX)
                id = topicChange.node_id();
            else
                assert(id == topicChange.node_id());
        }
        assert(id != UINT64_MAX);

        // Update fd -> nodeId map
        {
            std::lock_guard<std::mutex> lk(serverMtx);
            auto [it, inserted] = fdToNode.emplace(fd, 0);
            if (inserted)
                it->second = id;
            else
                assert(it->second == id);
        }

        // update hared node information and then broadcast to other nodes
        ApplyUpdate(msg);

        SPDLOG_INFO("broadcasting message");
        server->Broadcast(len, data);
    };

    // server started,
    // we should immediately get connections followed by node registration
    // from all living nodes
    // NOTE: for the sake of continuity we won't delete the existing nodes
    // NOTE: what happens if a node dies while server changeover is taking
    // place and never connects? Perhaps we could send a test ping to the client's address?
    server->LoopUntilShutdown(mShutdownFd, onConnect, onDisconnect, onMessage);
}

void TopologyManager::MainLoop() {
    while (!mShutdown) {
        RunClient();

        if (mShutdown) break;

        // if we exited the client then there might not be a server, try to create one
        usleep(rand() % 1000);
        RunServer();
    }
}
