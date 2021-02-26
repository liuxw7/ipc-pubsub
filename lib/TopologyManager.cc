#include "ipc_pubsub/TopologyManager.h"

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

#include "ipc_pubsub/TopologyServer.h"
#include "ipc_pubsub/UDSClient.h"
#include "protos/index.pb.h"

namespace ips {

// Managers a set of unix domain socket servers and clients.
TopologyManager::TopologyManager(const std::string& groupName, const std::string& nodeName,
                                 uint64_t nodeId, const std::string& dataPath,
                                 NodeChangeHandler onJoin, NodeChangeHandler onLeave,
                                 TopicChangeHandler onAnnounce, TopicChangeHandler onRetract,
                                 TopicChangeHandler onSubscribe, TopicChangeHandler onUnsubscribe)
    : mNodeId(nodeId),

      // Announce path should be hidden ('\0' start)
      mAnnouncePath(std::string(1, 0) + std::string(groupName)),
      mAddress(dataPath),
      mGroupName(groupName),
      mName(nodeName),
      mOnJoin(onJoin),
      mOnLeave(onLeave),
      mOnAnnounce(onAnnounce),
      mOnRetract(onRetract),
      mOnSubscribe(onSubscribe),
      mOnUnsubscribe(onUnsubscribe)

{
    SPDLOG_INFO("Creating {}:{}", mName, mNodeId);

    mMainThread = std::thread([this]() { MainLoop(); });
}

TopologyManager::~TopologyManager() {
    mShutdown = true;

    // Destroying the client should be sufficient to trigger its shutdown
    mClient = nullptr;
    mMainThread.join();
}

void TopologyManager::ApplyUpdate(size_t len, uint8_t* data) {
    TopologyMessage outerMsg;
    outerMsg.ParseFromArray(data, int(len));
    SPDLOG_INFO("{} Recieved :\n{}", mName, outerMsg.DebugString());
    ApplyUpdate(outerMsg);
}

void TopologyManager::ApplyUpdate(const TopologyMessage& msg) {
    std::unique_lock<std::mutex> lk(mMtx);

    auto seqComp = [](const TopologyMessage& lhs, const TopologyMessage& rhs) {
        return lhs.seq() < rhs.seq();
    };
    mHistory.insert(std::lower_bound(mHistory.begin(), mHistory.end(), msg, seqComp), msg);

    if (msg.has_node_change()) {
        const auto& nodeChange = msg.node_change();
        if (nodeChange.op() == ips::JOIN) {
            auto [it, inserted] = mNodes.emplace(nodeChange.id(), Node{});
            if (inserted) {
                it->second.id = nodeChange.id();
                it->second.name = nodeChange.name();
                it->second.address = nodeChange.address();
                if (mOnJoin) mOnJoin(nodeChange);
            } else {
                assert(it->second.id == nodeChange.id());
                if (it->second.name != nodeChange.name() ||
                    it->second.address != nodeChange.address()) {
                    // already existed, but changed, so update then return as a change that was
                    // applied
                    if (mOnJoin) mOnJoin(nodeChange);
                }
            }
        } else if (nodeChange.op() == ips::LEAVE) {
            auto it = mNodes.find(nodeChange.id());
            if (it != mNodes.end()) {
                mNodes.erase(it);
                if (mOnLeave) mOnLeave(nodeChange);
            }
        }
    } else if (msg.has_topic_change()) {
        const auto& topicChange = msg.topic_change();
        auto nit = mNodes.find(topicChange.node_id());
        if (nit == mNodes.end()) {
            SPDLOG_ERROR("Topic added with node id: {}, that hasn't joined, dropping",
                         topicChange.node_id());
            return;
        }
        if (topicChange.op() == ips::ANNOUNCE) {
            auto [tit, topicInserted] = nit->second.publications.emplace(
                topicChange.name(),
                Publication{.name = topicChange.name(), .mime = topicChange.mime()});
            if (topicInserted && mOnAnnounce) {
                mOnAnnounce(topicChange);
            } else if (topicChange.mime() != tit->second.mime) {
                tit->second.mime = topicChange.mime();
                mOnAnnounce(topicChange);
            }
        } else if (topicChange.op() == ips::SUBSCRIBE) {
            auto [tit, topicInserted] = nit->second.subscriptions.emplace(topicChange.name());
            if (topicInserted && mOnSubscribe) {
                mOnSubscribe(topicChange);
            }
        } else if (topicChange.op() == ips::UNSUBSCRIBE) {
            size_t numErased = nit->second.subscriptions.erase(topicChange.name());
            if (numErased > 0 && mOnUnsubscribe) {
                mOnUnsubscribe(topicChange);
            }
        } else if (topicChange.op() == ips::RETRACT) {
            size_t numErased = nit->second.publications.erase(topicChange.name());
            if (numErased > 0 && mOnRetract) {
                mOnRetract(topicChange);
            }
        }
    }
}

void TopologyManager::IntroduceOurselves(std::shared_ptr<UDSClient> client) {
    std::lock_guard<std::mutex> lk(mMtx);

    // send our complete history
    for (const auto& msg : mHistory) {
        client->Send(msg);
    }

    // craft messages about ourselves
    // TODO(micah) this will result in duplicates in the history everywhere, purge unecessary
    // messages on the server and notify the clients
    TopologyMessage msg;
    auto nodeMsg = msg.mutable_node_change();
    nodeMsg->set_id(mNodeId);
    nodeMsg->set_op(NodeOperation::JOIN);
    nodeMsg->set_name(mName);
    nodeMsg->set_address(mAddress);
    client->Send(msg);

    // send subscriptions
    auto it = mNodes.find(mNodeId);
    if (it != mNodes.end()) {
        for (const auto& pair : it->second.publications) {
            const Publication& pub = pair.second;
            auto topicMsg = msg.mutable_topic_change();
            topicMsg->set_name(pub.name);
            topicMsg->set_mime(pub.mime);
            topicMsg->set_node_id(mNodeId);
            topicMsg->set_op(ips::ANNOUNCE);
            client->Send(msg);
        }
        for (const auto& name : it->second.subscriptions) {
            auto topicMsg = msg.mutable_topic_change();
            topicMsg->set_name(name);
            topicMsg->set_node_id(mNodeId);
            topicMsg->set_op(ips::SUBSCRIBE);
            client->Send(msg);
        }
    }
}

void TopologyManager::Announce(const std::string& topic, const std::string& mime) {
    TopologyMessage outerMsg;
    auto msg = outerMsg.mutable_topic_change();
    msg->set_node_id(mNodeId);
    msg->set_op(TopicOperation::ANNOUNCE);
    msg->set_name(topic);
    msg->set_mime(mime);
    Send(outerMsg);
}

void TopologyManager::Retract(const std::string& topic) {
    TopologyMessage outerMsg;
    auto msg = outerMsg.mutable_topic_change();
    msg->set_node_id(mNodeId);
    msg->set_op(TopicOperation::RETRACT);
    msg->set_name(topic);
    Send(outerMsg);
}

void TopologyManager::Subscribe(const std::string& topic) {
    TopologyMessage outerMsg;
    auto msg = outerMsg.mutable_topic_change();
    msg->set_node_id(mNodeId);
    msg->set_op(TopicOperation::SUBSCRIBE);
    msg->set_name(topic);
    Send(outerMsg);
}

void TopologyManager::Unsubscribe(const std::string& topic) {
    TopologyMessage outerMsg;
    auto msg = outerMsg.mutable_topic_change();
    msg->set_node_id(mNodeId);
    msg->set_op(TopicOperation::UNSUBSCRIBE);
    msg->set_name(topic);
    std::lock_guard<std::mutex> lk(mMtx);
    Send(outerMsg);
}

void TopologyManager::Send(const TopologyMessage& msg) {
    std::lock_guard<std::mutex> lk(mMtx);
    if (mClient == nullptr) {
        mBacklog.push_back(msg);
        return;
    }

    int64_t ret = mClient->Send(msg);
    if (ret < 0) {
        SPDLOG_ERROR("Failed to send, destroying client");
        mClient = nullptr;
        mBacklog.push_back(msg);
        return;
    }
}

void TopologyManager::MainLoop() {
    // not necessarily running, but one TopologyManager will create one and
    // if the client drops it will attempt to create a new server
    std::shared_ptr<TopologyServer> server;

    while (!mShutdown) {
        // try to connect and wait before starting the server, if we fail to create the client then
        // we'll try to create the server, then go back to creating a client
        auto client = UDSClient::Create(
            mAnnouncePath, [this](size_t len, uint8_t* data) { ApplyUpdate(len, data); });
        if (client != nullptr) {
            // we're connected send our history so that the server can integrate it
            IntroduceOurselves(client);

            // clear backlog and update client
            std::vector<TopologyMessage> backlog;
            {
                std::lock_guard<std::mutex> lk(mMtx);
                mClient = client;
                backlog = mBacklog;
                mBacklog.clear();
            }
            for (const auto& msg : backlog) Send(msg);

            mClient->Wait();
            mClient = nullptr;
        }

        if (mShutdown) break;
        // we don't actually care if this server exists or not, we just need one
        // servier. If this fails it should be because there is another server
        // that the client can connect to. We'll seed the server with the clients history
        server = std::make_shared<TopologyServer>(mAnnouncePath, mHistory);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
}  // namespace ips
