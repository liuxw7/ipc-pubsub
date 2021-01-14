#include "IPCMessenger.h"

#include <fcntl.h> /* For O_* constants */
#include <semaphore.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <set>
#include <sstream>

#include "ShmMessage.h"
#include "Utils.h"
#include "protos/index.pb.h"

static void RemoveNode(uint64_t nodeId, ipc_pubsub::Index* index) {
    // remove from nodes
    auto inputNodes = std::move(*index->mutable_nodes());
    for (const auto& node : inputNodes) {
        if (node.id() != nodeId) *index->mutable_nodes()->Add() = node;
    }

    // remove from publishers/subscribers
    for (auto& topic : *index->mutable_topics()) {
        auto inputPublishers = std::move(*topic.mutable_publishers());
        for (const uint64_t pub : inputPublishers) {
            if (pub != nodeId) *topic.mutable_publishers()->Add() = pub;
        }

        auto inputSubscribers = std::move(*topic.mutable_subscribers());
        for (const uint64_t sub : inputSubscribers) {
            if (sub != nodeId) *topic.mutable_subscribers()->Add() = sub;
        }
    }
}

static void RemoveDeadNodes(ipc_pubsub::Index* index) {
    // copy because we'll be modifying index in place
    const auto oldNodes = index->nodes();
    for (const auto& node : oldNodes) {
        // issue signal 0, if success then it exists
        if (kill(node.pid(), 0) != 0) {
            // failed, doesn't exist, don't readd and remove from any subscriptions / publications
            std::cerr << "Remove " << node.pid() << " = " << node.id() << std::endl;
            RemoveNode(node.id(), index);
        }
    }
}

std::shared_ptr<IPCMessenger> IPCMessenger::Create(const char* ipcName, const char* nodeName) {
    std::random_device rd;
    std::mt19937_64 rng(rd());
    const uint64_t nodeId = rng();
    std::ostringstream oss;
    oss << "/" << std::hex << std::setw(16) << std::setfill('0') << nodeId;
    // Attempt to create or open
    int fd = shm_open(ipcName, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        std::cerr << "Failed to open IPC: " << strerror(errno) << std::endl;
        return nullptr;
    }

    std::cerr << "Input Sem: " << oss.str() << std::endl;
    sem_t* sem = sem_open(oss.str().c_str(), O_CREAT | O_EXCL, 0744, 0);
    if (sem == SEM_FAILED) {
        std::cerr << "Error, cannot open the sem: " << strerror(errno) << " (" << errno << ")"
                  << std::endl;
        close(fd);
        // shm_unlink(ipcName); TODO unlink when we are last one, probably at exit
        return nullptr;
    }

    return std::make_shared<IPCMessenger>(ipcName, fd, oss.str().c_str(), nodeName, nodeId, sem);
}

void IPCMessenger::OnNotify() {
    // Check current messages
    ipc_pubsub::Index index;
    flock(mShmFd, LOCK_EX);
    lseek(mShmFd, 0, SEEK_SET);
    index.ParseFromFileDescriptor(mShmFd);

    RemoveDeadNodes(&index);

    // find current node
    auto nodeIt = std::find_if(index.mutable_nodes()->begin(), index.mutable_nodes()->end(),
                               [this](const auto& node) { return node.id() == mNodeId; });

    // copy then clean inflight
    if (nodeIt != index.mutable_nodes()->end()) {
        for (const ipc_pubsub::InFlight& msg : nodeIt->in_flight()) {
            mDispatcher.Push(msg.topic(), msg.payload_name());
        }
        nodeIt->mutable_in_flight()->Clear();
    }

    flock(mShmFd, LOCK_UN);
}

void IPCMessenger::Shutdown() {
    std::cerr << "Shutdown" << std::endl;
    // end looping in thread
    mKeepGoing = false;

    // wake up thread to exit
    sem_post(mNotify);

    // wait for thread to join
    mNotifyThread.join();

    //// called either by deconstructor, or if shutdown is happening by the global atexit handler
    //{
    //    std::lock_guard<std::mutex> lk(gAllMessengers.mtx);
    //    gAllMessengers.messengers.erase(weak_from_this());
    //}

    // Remove outselves from the segment
    flock(mShmFd, LOCK_EX);
    lseek(mShmFd, 0, SEEK_SET);

    ipc_pubsub::Index index;
    index.ParseFromFileDescriptor(mShmFd);
    std::cerr << index.DebugString() << std::endl;

    // remove ourselves from nodes
    RemoveNode(mNodeId, &index);

    // write back
    ftruncate(mShmFd, index.ByteSizeLong());
    lseek(mShmFd, 0, SEEK_SET);
    index.SerializeToFileDescriptor(mShmFd);
    flock(mShmFd, LOCK_UN);

    sem_close(mNotify);
    std::cerr << "Unlinking: " << mNotifyName << std::endl;
    sem_unlink(mNotifyName.c_str());
    // shm_unlink(mIpcName.c_str()); // TODO unlink when we are last one out
}

IPCMessenger::~IPCMessenger() { Shutdown(); }

IPCMessenger::IPCMessenger(const char* ipcName, int shmFd, const char* notifyName,
                           const char* nodeName, uint64_t nodeId, sem_t* notify)
    : mKeepGoing(true),
      mShmFd(shmFd),
      mIpcName(ipcName),
      mNodeName(nodeName),
      mNodeId(nodeId),
      mNotifyName(notifyName),
      mNotify(notify) {
    //{
    //    std::lock_guard<std::mutex> lk(gAllMessengers.mtx);
    //    gAllMessengers.messengers.emplace(weak_from_this());
    //}

    ipc_pubsub::Index index;
    // Write message to end of shared memory segment
    flock(mShmFd, LOCK_EX);
    lseek(mShmFd, 0, SEEK_SET);

    index.ParseFromFileDescriptor(mShmFd);
    RemoveDeadNodes(&index);

    auto newNode = index.mutable_nodes()->Add();
    newNode->set_name(mNodeName);
    newNode->set_id(mNodeId);
    newNode->set_notify(mNodeName);
    newNode->set_pid(getpid());

    ftruncate(mShmFd, index.ByteSizeLong());
    lseek(mShmFd, 0, SEEK_SET);
    index.SerializeToFileDescriptor(mShmFd);
    flock(mShmFd, LOCK_UN);

    // read from semaphor until we're done
    mNotifyThread = std::thread([this]() {
        while (mKeepGoing) {
            std::cerr << "Waiting on " << mNotifyName << std::endl;
            sem_wait(mNotify);
            OnNotify();
        }
    });
}

bool IPCMessenger::Subscribe(std::string_view topicName, CallbackT cb) {
    // write subscription to meta
    ipc_pubsub::Index index;
    flock(mShmFd, LOCK_EX);
    lseek(mShmFd, 0, SEEK_SET);

    index.ParseFromFileDescriptor(mShmFd);
    RemoveDeadNodes(&index);

    ipc_pubsub::Topic* topic = nullptr;
    auto topicIt =
        std::find_if(index.mutable_topics()->begin(), index.mutable_topics()->end(),
                     [topicName](const auto& topicMsg) { return topicMsg.name() == topicName; });

    if (topicIt == index.mutable_topics()->end()) {
        // add it
        topic = index.mutable_topics()->Add();
        std::cerr << topic << std::endl;
        topic->set_name(topicName.data(), topicName.size());
    } else {
        topic = &*topicIt;
        std::cerr << topic << std::endl;
    }
    assert(topic != nullptr);

    if (std::count(topic->subscribers().begin(), topic->subscribers().end(), mNodeId) == 0) {
        *topic->mutable_subscribers()->Add() = mNodeId;
    }

    // write back
    ftruncate(mShmFd, index.ByteSizeLong());
    lseek(mShmFd, 0, SEEK_SET);
    index.SerializeToFileDescriptor(mShmFd);
    flock(mShmFd, LOCK_UN);

    // Publishers will check for updates before publishing, no nead to post semaphores
    mDispatcher.SetCallback(topicName, cb);
    return true;
}

bool IPCMessenger::Announce(std::string_view topicName, std::string_view mime) {
    std::cerr << "Announce: " << topicName << std::endl;
    // write topic to meta
    ipc_pubsub::Index index;
    flock(mShmFd, LOCK_EX);
    lseek(mShmFd, 0, SEEK_SET);

    index.ParseFromFileDescriptor(mShmFd);
    RemoveDeadNodes(&index);
    std::cerr << __LINE__ << ": " << index.DebugString() << std::endl;

    ipc_pubsub::Topic* topic = nullptr;
    auto topicIt =
        std::find_if(index.mutable_topics()->begin(), index.mutable_topics()->end(),
                     [topicName](const auto& topicMsg) { return topicMsg.name() == topicName; });

    if (topicIt == index.mutable_topics()->end()) {
        // add it
        topic = index.mutable_topics()->Add();
        topic->set_name(topicName.data(), topicName.size());
        topic->set_mime(mime.data(), mime.size());
    } else {
        topic = &*topicIt;
        if (topic->mime() != mime) {
            std::cerr << "Changing mime type " << topic->mime() << " -> " << mime << std::endl;
            topic->set_mime(mime.data(), mime.size());
        }
    }

    // add to publishers
    if (std::count(topic->publishers().begin(), topic->publishers().end(), mNodeId) == 0) {
        *topic->mutable_publishers()->Add() = mNodeId;
    }

    // write back and close
    ftruncate(mShmFd, index.ByteSizeLong());
    lseek(mShmFd, 0, SEEK_SET);
    index.SerializeToFileDescriptor(mShmFd);
    flock(mShmFd, LOCK_UN);

    return true;
}

bool IPCMessenger::Publish(std::string_view topicName, const uint8_t* data, size_t len) {
    auto buffer = ShmMessage::Create(len);
    if (buffer) {
        assert(buffer->Size() == len);
        std::copy(data, data + len, buffer->Data());
        return Publish(topicName, buffer);
    } else {
        return false;
    }
}

bool IPCMessenger::Publish(std::string_view topicName, std::shared_ptr<ShmMessage> buffer) {
    // write topic to meta
    ipc_pubsub::Index index;
    flock(mShmFd, LOCK_EX);
    lseek(mShmFd, 0, SEEK_SET);
    index.ParseFromFileDescriptor(mShmFd);
    RemoveDeadNodes(&index);
    std::cerr << index.DebugString() << std::endl;

    auto topicIt =
        std::find_if(index.mutable_topics()->begin(), index.mutable_topics()->end(),
                     [topicName](const auto& topicMsg) { return topicMsg.name() == topicName; });

    if (topicIt == index.mutable_topics()->end() || topicIt->mime().empty()) {
        std::cerr << "Publishing a topic that hasn't been announced!, Not publishing" << std::endl;
        flock(mShmFd, LOCK_UN);
        return false;
    } else {
        for (const uint64_t sub : topicIt->subscribers()) {
            for (auto node : *index.mutable_nodes()) {
                if (node.id() == sub) {
                    ipc_pubsub::InFlight* inFlight = node.mutable_in_flight()->Add();
                    inFlight->set_topic(topicName.data(), topicName.size());
                    inFlight->set_payload_name(buffer->Name().data(), buffer->Name().size());
                    buffer->IncBalance();
                }
            }
        }
    }

    // add to publishers
    if (std::count(topicIt->publishers().begin(), topicIt->publishers().end(), mNodeId) == 0) {
        *topicIt->mutable_publishers()->Add() = mNodeId;
    }

    // write back
    ftruncate(mShmFd, index.ByteSizeLong());
    lseek(mShmFd, 0, SEEK_SET);
    index.SerializeToFileDescriptor(mShmFd);
    flock(mShmFd, LOCK_UN);

    return true;
}
