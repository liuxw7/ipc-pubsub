
#include "TopologyStore.h"

#include "protos/index.pb.h"

using ipc_pubsub::NodeChange;
using ipc_pubsub::NodeOperation;
using ipc_pubsub::TopicChange;
using ipc_pubsub::TopicOperation;
using ipc_pubsub::TopologyMessage;

// static TopologyMessage SerializeNode(const TopologyStore::Node& node) {
//    TopologyMessage out;
//    auto nodeChange = out.mutable_node_changes()->Add();
//    nodeChange->set_id(node.id);
//    nodeChange->set_name(node.name);
//    nodeChange->set_address(node.address);
//    nodeChange->set_op(NodeOperation::JOIN);
//    for (const auto& sub : node.subscriptions) {
//        auto topicChange = out.mutable_topic_changes()->Add();
//        topicChange->set_name(sub);
//        topicChange->set_node_id(node.id);
//        topicChange->set_op(TopicOperation::SUBSCRIBE);
//    }
//
//    for (const auto& pubPair : node.publications) {
//        auto topicChange = out.mutable_topic_changes()->Add();
//        topicChange->set_name(pubPair.first);
//        topicChange->set_mime(pubPair.second);
//        topicChange->set_node_id(node.id);
//        topicChange->set_op(TopicOperation::ANNOUNCE);
//    }
//
//    return out;
//}
//
// TopologyMessage TopologyStore::ClearExcept(uint64_t keepNodeId) {
//    std::lock_guard<std::mutex> lk(mMtx);
//    TopologyMessage msg;
//    for (auto it = mNodeById.begin(); it != mNodeById.end();) {
//        if (it->first == keepNodeId) {
//            it++;
//            continue;
//        }
//
//        auto nodeChange = msg.mutable_node_changes()->Add();
//        nodeChange->set_op(ipc_pubsub::LEAVE);
//        nodeChange->set_id(it->first);
//
//        for (const auto& sub : it->second->subscriptions) {
//            auto topicChange = msg.mutable_topic_changes()->Add();
//            topicChange->set_name(sub);
//            topicChange->set_node_id(it->first);
//            topicChange->set_op(ipc_pubsub::UNSUBSCRIBE);
//        }
//        for (const auto& pubPair : it->second->publications) {
//            auto topicChange = msg.mutable_topic_changes()->Add();
//            topicChange->set_name(pubPair.first);
//            topicChange->set_node_id(it->first);
//            topicChange->set_op(ipc_pubsub::RETRACT);
//        }
//
//        it = mNodeById.erase(it);
//    }
//
//    return msg;
//}
//
// TopologyMessage TopologyStore::GetNodeMessage(uint64_t nodeId) {
//    std::lock_guard<std::mutex> lk(mMtx);
//
//    auto nodePtr = mNodeById.find(nodeId);
//    return SerializeNode(*nodePtr->second);
//}

bool operator==(const NodeChange& lhs, const TopologyStore::Node& rhs) {
    return lhs.id() == rhs.id && lhs.name() == rhs.name && lhs.address() == rhs.address;
}

bool operator==(const TopologyStore::Node& lhs, const NodeChange& rhs) { return rhs == lhs; }

bool operator!=(const TopologyStore::Node& lhs, const NodeChange& rhs) { return !(lhs == rhs); }

bool operator!=(const NodeChange& lhs, const TopologyStore::Node& rhs) { return !(lhs == rhs); }

void TopologyStore::ApplyUpdate(const TopologyMessage& msg) {
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
            } else if (*it->second != nodeChange) {
                // already existed, but changed, so update then return as a change that was
                applied it->second->id = nodeChange.id();
                it->second->name = nodeChange.name();
                it->second->address = nodeChange.address();
                *effectiveMsg.mutable_node_changes()->Add() = nodeChange;
            }
        } else if (nodeChange.op() == ipc_pubsub::LEAVE) {
            auto it = mNodeById.find(nodeChange.id());
            if (it != mNodeById.end()) {
                mNodeById.erase(it);
                *effectiveMsg.mutable_node_changes()->Add() = nodeChange;
            }
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
            auto [tit, topicInserted] =
                nit->second->publications.emplace(topicChange.name(), topicChange.mime());
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
