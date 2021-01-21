#include "TopologyServer.h"

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

#include "UDSServer.h"
#include "protos/index.pb.h"

using ipc_pubsub::NodeOperation;
using ipc_pubsub::TopicOperation;
using ipc_pubsub::TopologyMessage;

void TopologyServer::OnConnect(int fd) {
    // new client, send complete state message including all
    // nodes that we have a connection to
    TopologyMessage msg;
    {
        std::lock_guard<std::mutex> lk(mMtx);
        for (const auto& pair : mFdToNode) {
            msg.MergeFrom(store.GetNodeMessage(pair.second));
        }
    }

    SPDLOG_INFO("onConnect, sending message");
    bool sent = msg.SerializeToFileDescriptor(fd);
    if (!sent) {
        SPDLOG_ERROR("Failed to send topology");
    }
    SPDLOG_INFO("sent");
}

void TopologyServer::OnDisconnect(int fd) {
    TopologyMessage msg;
    {
        std::lock_guard<std::mutex> lk(mMtx);
        auto it = mFdToNode.find(fd);
        if (it == mFdToNode.end()) {
            SPDLOG_ERROR("Node connected but never identified itself");
            return;
        }
        auto nodeChangePtr = msg.mutable_node_changes()->Add();
        nodeChangePtr->set_id(it->second);
        nodeChangePtr->set_op(NodeOperation::LEAVE);

        // notifiy all clients that a client has been removed
        mFdToNode.erase(it);
    }
    ApplyUpdate(msg);

    SPDLOG_INFO("broadcasting disconnect message");
    {
        std::lock_guard<std::mutex> lk(mMtx);
        for (const auto& pair : mFdToNode) msg.SerializeToFileDescriptor(pair.first);
    }
}

void TopologyServer::OnData(int fd, int64_t len, uint8_t* data) {
    if (len == 0) return;

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
        std::lock_guard<std::mutex> lk(mMtx);
        auto [it, inserted] = mFdToNode.emplace(fd, 0);
        if (inserted)
            it->second = id;
        else
            assert(it->second == id);
    }

    // update hared node information and then broadcast to other nodes
    ApplyUpdate(msg);

    SPDLOG_INFO("broadcasting message");
    {
        std::lock_guard<std::mutex> lk(mMtx);
        for (const auto& pair : mFdToNode) msg.SerializeToFileDescriptor(pair.first);
    }
}

void TopologyServer::ApplyUpdate(const TopologyMessage& msg) { store.ApplyUpdate(msg); }

TopologyServer::TopologyServer(std::string_view announcePath) {
    // TODO should probably just make a TopologyServer type
    // If we are the leader then we need to keep track of which clients go with which nodes

    auto onConnect = [this](int fd) { OnConnect(fd); };
    auto onDisconnect = [this](int fd) { OnDisconnect(fd); };
    auto onMessage = [this](int fd, int64_t len, uint8_t* data) { OnData(fd, len, data); };
    mServer = UDSServer::Create(announcePath, onConnect, onDisconnect, onMessage);
}
