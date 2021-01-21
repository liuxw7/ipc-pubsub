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

// Managers a set of unix domain socket servers and clients.
TopologyManager::TopologyManager(std::string_view announcePath, std::string_view name,
                                 NodeChangeHandler onJoin, NodeChangeHandler onLeave,
                                 TopicChangeHandler onAnnounce, TopicChangeHandler onRetract,
                                 TopicChangeHandler onSubscribe, TopicChangeHandler onUnsubscribe)
    : mAnnouncePath(announcePath),
      mName(name),
      mOnJoin(onJoin),
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

    // Announce path should be hidden ('\0' start)
    mAnnouncePath.resize(announcePath.size() + 1);
    mAnnouncePath[0] = '\0';
    std::copy(announcePath.begin(), announcePath.end(), mAnnouncePath.begin() + 1);

    mStore = std::make_shared<TopologyStore>();

    // add ourselves to the list of nodes
    std::ostringstream oss;
    oss << '\0' << std::hex << std::setw(16) << std::setfill('0') << mNodeId;
    mAddress = oss.str();

    TopologyMessage msg;
    auto nodeMsg = msg.mutable_node_changes()->Add();
    nodeMsg->set_id(mNodeId);
    nodeMsg->set_op(NodeOperation::JOIN);
    nodeMsg->set_name(mName);
    nodeMsg->set_address(oss.str());
    mStore->ApplyUpdate(msg);

    mMainThread = std::thread([this]() { MainLoop(); });
}

TopologyManager::~TopologyManager() {
    // very large number so everything receives and decremenets but not UINT64_MAX so we don't roll
    // over
    mShutdown = true;
    mMainThread.join();
}

void TopologyManager::MainLoop() {
    auto onMessage = [this](size_t len, uint8_t* data) {
        SPDLOG_INFO("{} bytes recieved by client", len);
        TopologyMessage outerMsg;
        outerMsg.ParseFromArray(data, int(len));
        outerMsg = mStore->ApplyUpdate(outerMsg);

        for (const auto& node : outerMsg.node_changes()) {
            if (node.op() == NodeOperation::JOIN)
                mOnJoin(node);
            else if (node.op() == NodeOperation::LEAVE)
                mOnLeave(node);
        }
        for (const auto& topic : outerMsg.topic_changes()) {
            if (topic.op() == TopicOperation::ANNOUNCE)
                mOnAnnounce(topic);
            else if (topic.op() == TopicOperation::RETRACT)
                mOnRetract(topic);
            else if (topic.op() == TopicOperation::SUBSCRIBE)
                mOnSubscribe(topic);
            else if (topic.op() == TopicOperation::UNSUBSCRIBE)
                mOnUnsubscribe(topic);
        }
    };

    while (!mShutdown) {
        mClient = UDSClient::Create(mAnnouncePath, onMessage);
        if (mClient != nullptr) {
            // client connected send our details
            mClient->Send(mStore->GetNodeMessage(mNodeId));
            mClient->Wait();
        }

        // we don't actually care if the server exists or not, if this fails
        // it should be because there is another server that the client can
        // connect to
        mServer = std::make_shared<TopologyServer>(mAnnouncePath);

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
