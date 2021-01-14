#include "IPCMessenger.h"

#include <fcntl.h> /* For O_* constants */
#include <semaphore.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <mutex>
#include <random>
#include <set>

#include "ShmMessage.h"
#include "Utils.h"
#include "protos/index.pb.h"

struct OnReturn {
    OnReturn(std::function<void()> cleanup) : mCleanup(cleanup) {}
    ~OnReturn() { mCleanup(); }
    std::function<void()> mCleanup;
};

// template <typename T>
// struct WeakCompare {
//    bool operator()(const std::weak_ptr<T>& lhs, const std::weak_ptr<T>& rhs) const {
//        return lhs.owner_before(rhs);
//    }
//};
//
// struct AllMessengers {
//    ~AllMessengers() {
//        std::cerr << "Cleaning up" << std::endl;
//        std::lock_guard<std::mutex> lk(mtx);
//        for (std::weak_ptr<IPCMessenger> self : messengers) {
//            std::shared_ptr<IPCMessenger> locked = self.lock();
//            if (locked) {
//                locked->Shutdown();
//            }
//        }
//    }
//
//    std::mutex mtx;
//    std::set<std::weak_ptr<IPCMessenger>, WeakCompare<IPCMessenger>> messengers;
//} gAllMessengers;

std::shared_ptr<IPCMessenger> IPCMessenger::Create(const char* ipcName, const char* nodeName) {
    std::random_device rd;
    std::mt19937_64 rng(rd());
    const uint64_t nodeId = rng();
    char semName[18] = "/0000000000000000";
    ToHexString(nodeId, &semName[1]);
    // Attempt to create or open
    int fd = shm_open(ipcName, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        std::cerr << "Failed to open IPC: " << strerror(errno) << std::endl;
        return nullptr;
    }

    std::cerr << "Input Sem: " << semName << std::endl;
    sem_t* sem = sem_open(semName, O_CREAT | O_EXCL, 0744, 0);
    if (sem == SEM_FAILED) {
        std::cerr << "Error, cannot open the sem: " << strerror(errno) << " (" << errno << ")"
                  << std::endl;
        close(fd);
        // shm_unlink(ipcName); TODO unlink when we are last one, probably at exit
        return nullptr;
    }

    return std::make_shared<IPCMessenger>(ipcName, fd, semName, nodeName, nodeId, sem);
}

void IPCMessenger::OnNotify() {
    // Check current messages
    ipc_pubsub::Index index;
    flock(mShmFd, LOCK_EX);
    lseek(mShmFd, 0, SEEK_SET);
    index.ParseFromFileDescriptor(mShmFd);

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

    // remove ourselves from nodes
    std::remove_if(index.mutable_nodes()->begin(), index.mutable_nodes()->end(),
                   [this](const auto& node) { return node.id() == mNodeId; });

    // remove ourselves from publishers/subscribers
    for (auto& topic : *index.mutable_topics()) {
        std::remove_if(topic.mutable_publishers()->begin(), topic.mutable_publishers()->end(),
                       [this](uint64_t id) { return id == mNodeId; });
        std::remove_if(topic.mutable_subscribers()->begin(), topic.mutable_subscribers()->end(),
                       [this](uint64_t id) { return id == mNodeId; });
    }

    // write back
    ftruncate(mShmFd, 0);
    index.SerializeToFileDescriptor(mShmFd);
    flock(mShmFd, LOCK_UN);

    sem_close(mNotify);
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
    auto newNode = index.mutable_nodes()->Add();
    newNode->set_name(mNodeName);
    newNode->set_id(mNodeId);
    newNode->set_notify(mNodeName);

    ftruncate(mShmFd, 0);
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

    ipc_pubsub::Topic* topic = nullptr;
    auto topicIt =
        std::find_if(index.mutable_topics()->begin(), index.mutable_topics()->end(),
                     [topicName](const auto& topicMsg) { return topicMsg.name() == topicName; });

    if (topicIt == index.mutable_topics()->end()) {
        // add it
        topic = index.mutable_topics()->Add();
        topic->set_name(topicName.data(), topicName.size());
    } else {
        topic = &*topicIt;
    }

    if (std::count(topic->subscribers().begin(), topic->subscribers().end(), mNodeId) == 0) {
        *topic->mutable_subscribers()->Add() = mNodeId;
    }

    // write back and close
    ftruncate(mShmFd, 0);
    index.SerializeToFileDescriptor(mShmFd);
    flock(mShmFd, LOCK_UN);

    // Publishers will check for updates before publishing, no nead to post semaphores
    mDispatcher.SetCallback(topicName, cb);
    return true;
}

bool IPCMessenger::Announce(std::string_view topicName, std::string_view mime) {
    // write topic to meta
    ipc_pubsub::Index index;
    flock(mShmFd, LOCK_EX);
    lseek(mShmFd, 0, SEEK_SET);

    index.ParseFromFileDescriptor(mShmFd);

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
    ftruncate(mShmFd, 0);
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

    ipc_pubsub::Topic* topic = nullptr;
    auto topicIt =
        std::find_if(index.mutable_topics()->begin(), index.mutable_topics()->end(),
                     [topicName](const auto& topicMsg) { return topicMsg.name() == topicName; });

    if (topicIt == index.mutable_topics()->end() || topicIt->mime().empty()) {
        std::cerr << "Publishing a topic that hasn't been announced!, Not publishing" << std::endl;
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
    if (std::count(topic->publishers().begin(), topic->publishers().end(), mNodeId) == 0) {
        *topic->mutable_publishers()->Add() = mNodeId;
    }

    // write back and close
    ftruncate(mShmFd, 0);
    index.SerializeToFileDescriptor(mShmFd);
    flock(mShmFd, LOCK_UN);

    return true;
}
