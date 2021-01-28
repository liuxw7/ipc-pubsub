#include "TopologyManager.h"

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

#include "TopologyServer.h"
#include "UDSClient.h"
#include "protos/index.pb.h"

using ipc_pubsub::NodeOperation;
using ipc_pubsub::TopicOperation;
using ipc_pubsub::TopologyMessage;

// Managers a set of unix domain socket servers and clients.
TopologyManager::TopologyManager(std::string_view groupName, std::string_view nodeName,
                                 uint64_t nodeId, std::string_view dataPath,
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

void TopologyManager::Shutdown() {
    // Destroying the server and client should be sufficient to trigger their shutdown
    mShutdown = true;

    // copy client and server to prevent races with the main thread
    auto client = mClient;
    auto server = mServer;

    // shuting threse down will cause the main loop to stop running them and
    // check mShutdown
    if (client) client->Shutdown();
    if (server) server->Shutdown();

    mClient = nullptr;
    mServer = nullptr;
}

TopologyManager::~TopologyManager() {
    Shutdown();
    mMainThread.join();
}

void TopologyManager::ApplyUpdate(const TopologyMessage& msg) {
    std::unique_lock<std::mutex> lk(mMtx);
    mHistory.push_back(msg);
    if (msg.has_node_change()) {
        const auto& nodeChange = msg.node_change();
        if (nodeChange.op() == ipc_pubsub::JOIN) {
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
        } else if (nodeChange.op() == ipc_pubsub::LEAVE) {
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
        if (topicChange.op() == ipc_pubsub::ANNOUNCE) {
            auto [tit, topicInserted] = nit->second.publications.emplace(
                topicChange.name(),
                Publication{.name = topicChange.name(), .mime = topicChange.mime()});
            if (topicInserted && mOnAnnounce) {
                mOnAnnounce(topicChange);
            } else if (topicChange.mime() != tit->second.mime) {
                tit->second.mime = topicChange.mime();
                mOnAnnounce(topicChange);
            }
        } else if (topicChange.op() == ipc_pubsub::SUBSCRIBE) {
            auto [tit, topicInserted] = nit->second.subscriptions.emplace(topicChange.name());
            if (topicInserted && mOnSubscribe) {
                mOnSubscribe(topicChange);
            }
        } else if (topicChange.op() == ipc_pubsub::UNSUBSCRIBE) {
            size_t numErased = nit->second.subscriptions.erase(topicChange.name());
            if (numErased > 0 && mOnUnsubscribe) {
                mOnUnsubscribe(topicChange);
            }
        } else if (topicChange.op() == ipc_pubsub::RETRACT) {
            size_t numErased = nit->second.publications.erase(topicChange.name());
            if (numErased > 0 && mOnRetract) {
                mOnRetract(topicChange);
            }
        }
    }
}
void TopologyManager::SetNewClient(std::shared_ptr<UDSClient> newClient) {
    std::unique_lock<std::mutex> lk(mMtx);
    mClient = newClient;
    TopologyMessage msg;
    auto nodeMsg = msg.mutable_node_change();
    nodeMsg->set_id(mNodeId);
    nodeMsg->set_op(NodeOperation::JOIN);
    nodeMsg->set_name(mName);
    nodeMsg->set_address(mAddress);
    mClient->Send(msg);

    // send subscriptions
    auto it = mNodes.find(mNodeId);
    if (it != mNodes.end()) {
        for (const auto& pair : it->second.publications) {
            const Publication& pub = pair.second;
            auto topicMsg = msg.mutable_topic_change();
            topicMsg->set_name(pub.name);
            topicMsg->set_mime(pub.mime);
            topicMsg->set_node_id(mNodeId);
            topicMsg->set_op(ipc_pubsub::ANNOUNCE);
            mClient->Send(msg);
        }
        for (const auto& name : it->second.subscriptions) {
            auto topicMsg = msg.mutable_topic_change();
            topicMsg->set_name(name);
            topicMsg->set_node_id(mNodeId);
            topicMsg->set_op(ipc_pubsub::SUBSCRIBE);
            mClient->Send(msg);
        }
    }
}

void TopologyManager::MainLoop() {
    auto onMessage = [this](size_t len, uint8_t* data) {
        TopologyMessage outerMsg;
        outerMsg.ParseFromArray(data, int(len));
        SPDLOG_INFO("{} Recieved :\n{}", mName, outerMsg.DebugString());
        ApplyUpdate(outerMsg);
    };

    while (!mShutdown) {
        auto newClient = UDSClient::Create(mAnnouncePath, onMessage);
        if (newClient != nullptr) {
            // client connected send our details
            // send information about ourself
            SetNewClient(newClient);

            mClient->Wait();
            mClient = nullptr;
        }

        if (mShutdown) break;
        // we don't actually care if the server exists or not, if this fails
        // it should be because there is another server that the client can
        // connect to
        mServer = std::make_shared<TopologyServer>(mAnnouncePath, mHistory);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
