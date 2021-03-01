#include "ipc_pubsub/TopologyServer.h"

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

#include "ipc_pubsub/UDSServer.h"
#include "protos/index.pb.h"
namespace ips {

void TopologyServer::Shutdown() { mServer->Shutdown(); }

TopologyServer::~TopologyServer() { mPurgeThread.join(); }

void TopologyServer::OnConnect(int fd) {
    // new client, send complete state message including all
    // nodes that we have a connection to
    std::lock_guard<std::mutex> lk(mMtx);

    SPDLOG_DEBUG("onConnect, sending digest");
    auto& client = mClients[fd];
    for (const auto& msg : mHistory) {
        bool sent = msg.SerializeToFileDescriptor(fd);
        if (!sent) {
            SPDLOG_ERROR("Failed to send topology");
        }
        assert(msg.seq() >= client.seq);
        client.seq = msg.seq();
    }
}

void TopologyServer::PruneRemoved() {
    std::unordered_set<uint64_t> left;
    for (const auto& msg : mHistory) {
        if (msg.has_node_change() && msg.node_change().op() == NodeOperation::LEAVE)
            left.emplace(msg.node_change().id());
    }

    // remove all messages for nodes that have been removed (nodes with LEAVE messages)
    for (auto it = mHistory.begin(); it != mHistory.end();) {
        if (it->has_node_change()) {
            if (left.count(it->node_change().id()) > 0 &&
                it->node_change().op() != NodeOperation::LEAVE) {
                // node has left and this isn't the leave message, remove
                it = mHistory.erase(it);
            } else {
                ++it;
            }
        } else if (it->has_topic_change()) {
            if (left.count(it->topic_change().node_id() > 0)) {
                // topic change for a node that doesn't exist, remove it
                it = mHistory.erase(it);
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }
}

void TopologyServer::Broadcast() {
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

    PruneRemoved();
    Broadcast();
}

// adapted from String.hashCode()

bool operator==(const NodeChange& lhs, const NodeChange& rhs) {
    return lhs.id() == rhs.id() && lhs.op() == rhs.op() && lhs.name() == rhs.name() &&
           lhs.address() == rhs.address();
}

bool operator==(const TopicChange& lhs, const TopicChange& rhs) {
    return lhs.name() == rhs.name() && lhs.mime() == rhs.mime() && lhs.node_id() == rhs.node_id() &&
           lhs.op() == rhs.op() && lhs.socket() == rhs.socket();
}

bool operator==(const TopologyMessage& lhs, const TopologyMessage& rhs) {
    return lhs.seq() == rhs.seq() && lhs.uid() == rhs.uid() &&
           lhs.has_node_change() == rhs.has_node_change() &&
           lhs.has_topic_change() == rhs.has_topic_change() &&
           ((lhs.has_node_change() && lhs.node_change() == rhs.node_change()) ||
            (lhs.has_topic_change() && lhs.topic_change() == rhs.topic_change()));
}

bool TopologyServer::IsRedundant(const TopologyMessage& msg, const Client& client) {
    for (const auto& hMsg : client.history) {
        if (hMsg.has_node_change() && msg.has_node_change() &&
            hMsg.node_change() == msg.node_change()) {
            return true;
        } else if (hMsg.has_topic_change() && msg.has_topic_change() &&
                   hMsg.topic_change() == msg.topic_change()) {
            return true;
        }
    }
    return false;
}

void TopologyServer::AddNovelMessage(const TopologyMessage& msg, Client* client) {
    static std::random_device randomDevice;
    static std::mt19937_64 randomGen(randomDevice());
    // mint new seq and add to history if the message is fresh (no sequence)
    if (IsRedundant(msg, *client)) {
        SPDLOG_INFO("Redundant message ignored: {}", msg.ShortDebugString());
    } else {
        TopologyMessage tmp = msg;
        tmp.set_seq(mNextSeq++);
        tmp.set_uid(randomGen());
        mHistory.push_back(tmp);
        client->history.push_back(tmp);
    }
}

void TopologyServer::AddOldMessage(const TopologyMessage& msg, Client* client) {
    // see if we have this message, if not then insert into history
    auto hit = std::lower_bound(mHistory.begin(), mHistory.end(), msg,
                                [](const TopologyMessage& lhs, const TopologyMessage& rhs) {
                                    return lhs.seq() < rhs.seq();
                                });
    if (hit == mHistory.end()) {
        // we don't have this yet and it is the newest, accept
        assert(msg.uid() != 0);
        mHistory.insert(hit, msg);
        client->history.push_back(msg);
        mNextSeq = msg.seq() + 1;
    } else if (hit->seq() == msg.seq() && hit->uid() == msg.uid()) {
        // we have a match, just ignore
        assert(*hit == msg);
    } else {
        // either the sequence isn't recent or has a different UID.
        // The reason not to fill non-recent messages is that presumably
        // the hole is there fore some reason -- for instance because the
        // node is shutdown and the nodes history has been purged.
        SPDLOG_ERROR("Received conflicting historic message, rejecting\n{}",
                     msg.ShortDebugString());
    }
}

void TopologyServer::OnData(int fd, int64_t len, uint8_t* data) {
    if (len == 0) return;

    TopologyMessage msg;
    msg.ParseFromArray(data, int(len));

    // get an ID out of the message (should only have one, clients should only
    // send information about themselves)
    uint64_t nodeId = 0;
    if (msg.has_node_change())
        nodeId = msg.node_change().id();
    else if (msg.has_topic_change())
        nodeId = msg.topic_change().node_id();
    if (nodeId == 0) {
        SPDLOG_ERROR("Ignoring msg with no node ID: {}", msg.ShortDebugString());
        return;
    }

    std::lock_guard<std::mutex> lk(mMtx);

    // Update fd -> nodeId map
    auto cit = mClients.find(fd);
    assert(cit != mClients.end());  // should have created when the client connected
    if (cit->second.nodeId != nodeId) {
        SPDLOG_ERROR("Node changed ID?");
    }
    cit->second.nodeId = nodeId;

    // Update Message History
    if (msg.seq() == 0) {
        AddNovelMessage(msg, &cit->second);
    } else {
        AddOldMessage(msg, &cit->second);
    }

    // Remove Unused
    PruneRemoved();

    // ensure all clients are up-to-date with changes
    Broadcast();
}

void TopologyServer::PurgeDisconnected() {
    // one time, at the beginning of the server nodes that
    // aren't connected will be remove (we'll send LEAVE messages)
    std::lock_guard<std::mutex> lk(mMtx);
    std::unordered_map<uint64_t, int> connected;
    for (const auto& pair : mClients) {
        connected.emplace(pair.second.nodeId, pair.first);
    }

    std::unordered_set<uint64_t> historicallyActive;
    for (const auto& msg : mHistory) {
        if (!msg.has_node_change()) continue;

        if (msg.node_change().op() == ips::JOIN) {
            historicallyActive.emplace(msg.node_change().id());
        } else if (msg.node_change().op() == ips::LEAVE) {
            historicallyActive.erase(msg.node_change().id());
        }
    }

    // construct LEAVE messages for each historically active node that isn't
    // connected
    for (uint64_t id : historicallyActive) {
        if (connected.count(id) == 0) {
            auto& msg = mHistory.emplace_back();
            msg.set_seq(mNextSeq++);
            auto nodeChangePtr = msg.mutable_node_change();
            nodeChangePtr->set_id(id);
            nodeChangePtr->set_op(NodeOperation::LEAVE);
        }
    }

    // Remove Unused
    PruneRemoved();

    // Update Clients
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
}  // namespace ips
