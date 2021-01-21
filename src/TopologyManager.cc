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

#include "TopologyServer.h"
#include "TopologyStore.h"
#include "UDSClient.h"
#include "protos/index.pb.h"

using ipc_pubsub::NodeOperation;
using ipc_pubsub::TopicOperation;
using ipc_pubsub::TopologyMessage;
constexpr size_t MAX_NOTIFY_SIZE = 2048;
// Managers a set of unix domain socket servers and clients.

TopologyManager::TopologyManager(std::string_view announcePath, std::string_view name,
                                 NodeChangeHandler onJoin, NodeChangeHandler onLeave,
                                 TopicChangeHandler onAnnounce, TopicChangeHandler onRetract,
                                 TopicChangeHandler onSubscribe, TopicChangeHandler onUnsubscribe)
    : mOnJoin(onJoin),
      mOnLeave(onLeave),
      mOnAnnounce(onAnnounce),
      mOnRetract(onRetract),
      mOnSubscribe(onSubscribe),
      mOnUnsubscribe(onUnsubscribe)

{
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

    mMainThread = std::thread([this]() { MainLoop(); });

    mStore = std::make_shared<TopologyStore>();
}

TopologyManager::~TopologyManager() {
    // very large number so everything receives and decremenets but not UINT64_MAX so we don't roll
    // over
    size_t shutItDown = UINT32_MAX;
    mShutdown = true;
    mMainThread.join();
}

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
    std::unique_lock<std::mutex> lk(mMtx);
    for (const auto& nodeChange : msg.node_changes()) {
        if (nodeChange.op() == ipc_pubsub::JOIN) {
            auto [it, inserted] = mNodeById.emplace(nodeChange.id(), nullptr);
            if (inserted) {
                it->second = std::make_shared<Node>();
                it->second->id = nodeChange.id();
                it->second->name = nodeChange.name();
                it->second->address = nodeChange.address();

                lk.unlock();
                if (mOnJoin) mOnJoin(nodeChange);
                lk.lock();
            }
        } else if (nodeChange.op() == ipc_pubsub::LEAVE) {
            auto it = mNodeById.find(nodeChange.id());
            mNodeById.erase(it);
            lk.unlock();
            if (mOnLeave) mOnLeave(nodeChange);
            lk.lock();
        }
    }
    for (const auto& topicChange : msg.topic_changes()) {
        auto [nit, nodeInserted] = mNodeById.emplace(topicChange.node_id(), nullptr);
        if (nodeInserted) {
            nit->second = std::make_shared<Node>();
            nit->second->id = topicChange.node_id();
        }

        if (topicChange.op() == ipc_pubsub::ANNOUNCE) {
            auto [tit, topicInserted] = nit->second->publications.emplace(
                topicChange.name(),
                Publication{.name = topicChange.name(), .mime = topicChange.mime()});
            if (topicInserted) {
                lk.unlock();
                if (mOnAnnounce) mOnAnnounce(topicChange);
                lk.lock();
            }
            // TODO validate that the MIME doesn't change?
        } else if (topicChange.op() == ipc_pubsub::SUBSCRIBE) {
            auto [tit, topicInserted] = nit->second->subscriptions.emplace(topicChange.name());
            if (topicInserted) {
                lk.unlock();
                if (mOnSubscribe) mOnSubscribe(topicChange);
                lk.lock();
            }
        } else if (topicChange.op() == ipc_pubsub::UNSUBSCRIBE) {
            size_t numErased = nit->second->subscriptions.erase(topicChange.name());
            if (numErased > 0) {
                lk.unlock();
                if (mOnUnsubscribe) mOnUnsubscribe(topicChange);
                lk.lock();
            }
        } else if (topicChange.op() == ipc_pubsub::RETRACT) {
            size_t numErased = nit->second->publications.erase(topicChange.name());
            if (numErased > 0) {
                lk.unlock();
                if (mOnRetract) mOnRetract(topicChange);
                lk.lock();
            }
        }
    }
}

void TopologyManager::MainLoop() {
    auto onMessage = [this](size_t len, uint8_t* data) {
        SPDLOG_INFO("{} bytes recieved by client", len);
        TopologyMessage msg;
        msg.ParseFromArray(data, int(len));
        ApplyUpdate(msg);
    };

    while (!mShutdown) {
        mClient = UDSClient::Create(mAnnouncePath, onMessage);
        if (mClient != nullptr) {
            // client connected send our details
            mClient->Send(GetNodeMessage(mNodeId));
            mClient->Wait();
        }

        // we don't actually care if the server exists or not, if this fails
        // it should be because there is another server that the client can
        // connect to
        mServer = std::make_shared<TopologyServer>(mAnnouncePath);

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
