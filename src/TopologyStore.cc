
#include "TopologyStore.h"

#include "protos/index.pb.h"

using ipc_pubsub::NodeOperation;
using ipc_pubsub::TopicOperation;
using ipc_pubsub::TopologyMessage;

static TopologyMessage SerializeNode(const TopologyStore::Node& node) {
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

TopologyMessage TopologyStore::GetNodeMessage(uint64_t nodeId) {
    std::lock_guard<std::mutex> lk(mMtx);
    auto nodePtr = mNodeById.find(nodeId);
    return SerializeNode(*nodePtr->second);
}

ipc_pubsub::TopologyMessage TopologyStore::ApplyUpdate(const TopologyMessage& msg) {
    ipc_pubsub::TopologyMessage effectiveMsg;

    std::unique_lock<std::mutex> lk(mMtx);
    for (const auto& nodeChange : msg.node_changes()) {
        if (nodeChange.op() == ipc_pubsub::JOIN) {
            auto [it, inserted] = mNodeById.emplace(nodeChange.id(), nullptr);
            if (inserted) {
                it->second = std::make_shared<Node>();
                it->second->id = nodeChange.id();
                it->second->name = nodeChange.name();
                it->second->address = nodeChange.address();

                *effectiveMsg.mutable_node_changes()->Add() = nodeChange;
            }
        } else if (nodeChange.op() == ipc_pubsub::LEAVE) {
            auto it = mNodeById.find(nodeChange.id());
            mNodeById.erase(it);
            *effectiveMsg.mutable_node_changes()->Add() = nodeChange;
        }
    }
    for (const auto& topicChange : msg.topic_changes()) {
        auto nit = mNodeById.find(topicChange.node_id());
        if (nit == mNodeById.end()) {
            SPDLOG_ERROR("Topic added with node id: {}, that hasn't joined, dropping",
                         topicChange.node_id());
            continue;
        }
        if (topicChange.op() == ipc_pubsub::ANNOUNCE) {
            auto [tit, topicInserted] = nit->second->publications.emplace(
                topicChange.name(),
                Publication{.name = topicChange.name(), .mime = topicChange.mime()});
            if (topicInserted) {
                *effectiveMsg.mutable_topic_changes()->Add() = topicChange;
            }
            // TODO validate that the MIME doesn't change?
        } else if (topicChange.op() == ipc_pubsub::SUBSCRIBE) {
            auto [tit, topicInserted] = nit->second->subscriptions.emplace(topicChange.name());
            if (topicInserted) {
                *effectiveMsg.mutable_topic_changes()->Add() = topicChange;
            }
        } else if (topicChange.op() == ipc_pubsub::UNSUBSCRIBE) {
            size_t numErased = nit->second->subscriptions.erase(topicChange.name());
            if (numErased > 0) {
                *effectiveMsg.mutable_topic_changes()->Add() = topicChange;
            }
        } else if (topicChange.op() == ipc_pubsub::RETRACT) {
            size_t numErased = nit->second->publications.erase(topicChange.name());
            if (numErased > 0) {
                *effectiveMsg.mutable_topic_changes()->Add() = topicChange;
            }
        }
    }
    return effectiveMsg;
}
