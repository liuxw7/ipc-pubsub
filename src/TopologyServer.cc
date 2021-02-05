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

void TopologyServer::Shutdown() { mServer->Shutdown(); }

TopologyServer::~TopologyServer() { mPurgeThread.join(); }

void TopologyServer::OnConnect(int fd) {
    // new client, send complete state message including all
    // nodes that we have a connection to
    std::lock_guard<std::mutex> lk(mMtx);
    SPDLOG_INFO("onConnect, sending digest");
    auto& client = mClients[fd];
    for (const auto& msg : mHistory) {
        bool sent = msg.SerializeToFileDescriptor(fd);
        if (!sent) {
            SPDLOG_ERROR("Failed to send topology");
        }
        SPDLOG_ERROR("{} vs {}", msg.seq(), client.seq);
        assert(msg.seq() >= client.seq);
        client.seq = msg.seq();
    }

    SPDLOG_INFO("sent");
}

void TopologyServer::Broadcast() {
    std::lock_guard<std::mutex> lk(mMtx);
    for (auto& pair : mClients) {
        for (const auto& msg : mHistory) {
            if (msg.seq() > pair.second.seq) {
                assert(msg.seq() < mNextSeq);
                msg.SerializeToFileDescriptor(pair.first);
                pair.second.seq = msg.seq();
            }
        }
    }
}

void TopologyServer::OnDisconnect(int fd) {
    {
        std::lock_guard<std::mutex> lk(mMtx);
        auto it = mClients.find(fd);
        assert(it != mClients.end());  // how can a fd disconnect that wasn't connected?
        auto& msg = mHistory.emplace_back();
        msg.set_seq(mNextSeq++);
        auto nodeChangePtr = msg.mutable_node_change();
        nodeChangePtr->set_id(it->second.nodeId);
        nodeChangePtr->set_op(NodeOperation::LEAVE);

        // notifiy all clients that a client has been removed
        mClients.erase(it);
    }

    Broadcast();
}

void TopologyServer::OnData(int fd, int64_t len, uint8_t* data) {
    if (len == 0) return;

    // notify all clients that a new client has been updated
    TopologyMessage msg;
    msg.ParseFromArray(data, int(len));

    // get an ID out of the message (should only have one, clients should only
    // send information about themselves)
    uint64_t id = UINT64_MAX;
    if (msg.has_node_change())
        id = msg.node_change().id();
    else if (msg.has_topic_change())
        id = msg.topic_change().node_id();
    assert(id != UINT64_MAX);

    // Update fd -> nodeId map
    {
        std::lock_guard<std::mutex> lk(mMtx);

        auto it = mClients.find(fd);
        assert(it != mClients.end());  // should have created when the client connected
        it->second.nodeId = id;

        // mint new seq and add to history if the message is fresh (no sequence)
        if (msg.seq() == 0) {
            msg.set_seq(mNextSeq++);
            mHistory.push_back(msg);
        } else {
            // see if we have this message, if not then insert into history
            auto it = std::lower_bound(mHistory.begin(), mHistory.end(), msg,
                                       [](const TopologyMessage& lhs, const TopologyMessage& rhs) {
                                           return lhs.seq() < rhs.seq();
                                       });
            if (it == mHistory.end() || it->seq() != msg.seq()) {
                // new message, insert
                mHistory.insert(it, msg);
            }
        }
    }

    // ensure all clients are up-to-date with changes
    Broadcast();
}

void TopologyServer::PurgeDisconnected() {
    {
        // one time, at the beginning of the server any disconnected nodes have left messages sent
        std::lock_guard<std::mutex> lk(mMtx);
        std::unordered_map<uint64_t, int> connected;
        for (const auto& pair : mClients) {
            connected.emplace(pair.second.nodeId, pair.first);
        }

        std::unordered_set<uint64_t> historicallyActive;
        for (const auto& msg : mHistory) {
            if (!msg.has_node_change()) continue;

            if (msg.node_change().op() == ipc_pubsub::JOIN) {
                historicallyActive.emplace(msg.node_change().id());
            } else if (msg.node_change().op() == ipc_pubsub::LEAVE) {
                historicallyActive.erase(msg.node_change().id());
            }
        }

        // construct LEAVE messages for each historically active node that isn't connected
        for (uint64_t id : historicallyActive) {
            if (connected.count(id) == 0) {
                auto& msg = mHistory.emplace_back();
                msg.set_seq(mNextSeq++);
                auto nodeChangePtr = msg.mutable_node_change();
                nodeChangePtr->set_id(id);
                nodeChangePtr->set_op(NodeOperation::LEAVE);
            }
        }
    }
    Broadcast();
}

TopologyServer::TopologyServer(std::string_view announcePath,
                               const std::vector<TopologyMessage>& digest) {
    // On startup the topology server can be provided with a message containing a digest
    // this will be loaded as the starting server state
    mNextSeq = 1;
    mHistory = digest;
    for (const auto& msg : digest) {
        mNextSeq = msg.seq() + 1;
    }

    auto onConnect = [this](int fd) { OnConnect(fd); };
    auto onDisconnect = [this](int fd) { OnDisconnect(fd); };
    auto onMessage = [this](int fd, int64_t len, uint8_t* data) { OnData(fd, len, data); };
    mServer = UDSServer::Create(announcePath, onConnect, onDisconnect, onMessage);

    // after 200ms of being up, any nodes that haven't connected should be purged
    mPurgeThread = std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        PurgeDisconnected();
    });
}
