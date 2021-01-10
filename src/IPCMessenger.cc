#include "IPCMessenger.h"

#include <fcntl.h> /* For O_* constants */
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

#include "index.pb.h"

const int QUEUE_PERMS = (int)(0644);
const char MAGIC[8] = "MAGICPS";
const char SHMEM_NAME[] = "/ipc_pub_sub";

struct State {
    mqd_t notify;
};

struct OnReturn {
    OnReturn(std::function<void()> cleanup) : cleanup(cleanup) {}
    ~OnReturn() { cleanup(); }
    std::function<void()> cleanup;
};

struct Node {
    size_t next = 0;  // offset from the begginning of shm in bytes, 0 means null / no next
    size_t prev = 0;

    // Data in nodes *isn't* locked, so that we don't have to keep track of number of readers.
    // Instead we will optimistically read hash and data then check the data. If the data isn't
    // valid (stored hash != hash(data)) we assume that the message was popped off the queue before
    // the thread had a chance to read it, and then read the next message.
    size_t seq = 0;
    size_t hash = 0;  // sdbm algorithm of data string
    size_t len = 0;
    uint8_t data[0];
};

// Index Format

// ---------------------------------------------------
// name     | bytes | type    | Description
// ---------------------------------------------------
// MAGIC    |  8    | char[]  |
// VERSION  |  8    | UINT64  |
// INDEX    |  ?    | char[]  | Serialized protobuf

bool IPCMessenger::updateIndex(std::function<bool(Index* update)> cb) {
    flock(fd, LOCK_EX);
    OnReturn onRet([]() { flock(fd, LOCK_UN); });

    // read index
    Index index;
    lseek(fd, 16, SEEK_SET);
    index.ParseFromFileDescriptor(fd);

    // allow callback to update
    return cb(&index);
}

static bool registerNode(const char* nodeName, uint64_t nodeId, const char* queueName) {
    return updateIndex([](Index* update) {
        auto nodes = update->mutable_nodes();
        for (const auto& node : *nodes) {
            if (node.id() == nodeId) {
                // already registered
                std::cerr << nodeId << " with matching id already registered!" << std::endl;
                return false;
            }
        }

        auto newNode = nodes->Add();
        newNode->set_name(nodeName);
        newNode->set_id(nodeId);
        newNode->set_notify(queueName);
        return true;
    });
}

static bool unRegisterNode(uint64_t nodeId) {
    return updateIndex([](Index* update) {
        // copy then delete old nodes
        const auto oldNodes = update->nodes();
        update->mutable_nodes()->Clear();
        for (const auto& node : oldNodes) {
            if (node.id() != nodeId) {
                // doesn't match, so copy
                *update->mutable_nodes()->Add() = node;
            }
        }

        for (auto& topic : update->mutable_topics()) {
            // copy then delete old nodes from publishers
            const auto oldPublishers = topic->publishers();
            const auto oldSubscribers = topic->subscribers();
            topic->mutable_publishers()->Clear();
            topic->mutable_subscribers()->Clear();
            for (const uint64_t id : oldPublishers) {
                if (id != nodeId) {
                    *topic->mutable_publishers()->Add() = id;
                }
            }

            for (const uint64_t id : oldSubscribers) {
                if (id != nodeId) {
                    *topic->mutable_subscribers()->Add() = id;
                }
            }
        }
        return true;
    });
}

std::shared_ptr<IPCMessenger> IPCMessenger::Create(const char* ipcName, const char* nodeName) {
    static std::random_device rd;
    std::mt19937_64 rng(rd());
    const uint64_t id = rng();
    const std::string queueName = "/" + std::to_string(id);

    // create meta that stores actual connection

    // Attempt to create or open
    int fd = shm_open(ipcName, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        std::cerr << "Failed to open IPC: " << strerror(errno) << std::endl;
        return nullptr;
    }

    // check if magic is correct, need to lock because two processes might read
    // magic as missing at the same time then race to initialize, after this we
    // only used the mutex in Meta to control races
    flock(fd, LOCK_EX);
    OnReturn onRet([]() { flock(fd, LOCK_UN); });
    char magic[8];
    if (pread(fd, magic, 8, 0) != 8) {
        return nullptr;
    }

    if (strncmp(magic, MAGIC) != 0) {
        // not initialized resize and initialize
        Index index;
        if (ftruncate(fd, 8 + 8 + index.ByteSizeLong()) == -1) {
            std::cerr << "Failed to resize: " << strerror(errno) << std::endl;
            shm_unlink(ipcName);
            return nullptr;
        }
    }

    struct mq_attr attr = QUEUE_ATTR_INITIALIZER;
    mqd_t mq = mq_open(queueName.c_str(), O_CREAT | O_RDWR, QUEUE_PERMS, &attr);
    if (mq < 0) {
        std::cerr << "Error, cannot open the queue: " << strerror(errno) << std::endl;
        shm_unlink(ipcName);
        return nullptr;
    }

    return std::make_shared<IPCMessenger>(ipcName, fd, queueName, mq);
}

void IPCMessenger::onNotify(const NotifyMessage& msg) {}

void IPCMessenger::~IPCMessenger() {
    mq_close(mState->notify);
    mq_unlink(mState->notifyName);
    shm_unlink(mState->ipcName.c_str());
}

void IPCMessenger::IPCMessenger(const char* ipcName, int shmFd, const char* notifyName,
                                mqd_t notify)
    : mKeepGoing(true), mShmFd(shmFd), mNotifyName(notifyName), mIpcName(ipcName), mNotify(notify) {
    mNotifyThread = std::thread([this]() {
        unsigned int prio;
        ssize_t bytes_read;
        char buffer[HARD_MSGMAX + 1];
        NotifyMessage notifyMsg;
        while (mKeepGoing) {
            memset(buffer, 0x00, sizeof(buffer));
            bytesRead = mq_receive(mNotify, buffer, HARD_MSGMAX, &prio);
            if (bytesRead >= 0) {
                notifyMsg.ParseFromArray(data, bytesRead);
                onNotify(notifyMsg);
            }
        }
    });
    return messenger;
}

bool IPCMessenger::Subscribe(std::string_view topicName, std::string_view mime,
                             std::function<void(const uint8_t* data, size_t len)> cb) {
    // add ourselves to the list of subscribers, creating as necessary in the registry
    updateIndex([this](Index* index) {
        // insert topic if it doesn't exist
        auto topicIt =
            std::find_if(index->mutable_topics().begin(), index->mutable_topics().end(),
                         [topicName](const auto& topic) { return topic.name == topicName });
        Topic* topic = nullptr;
        if (topicIt == index->topics().end()) {
            topic = index->mutable_topics()->Add();
            topic->set_name(topicName);
            topic->set_mime(mime);
        } else {
            topic = &*it;
        }

        auto subIt = std::find(index->subscribers().begin(), index->subscribers().end(), mId);
        if (it == topic->subscribers().end()) {
            *topic->subscribers->Add() = mId;
        }
    });

    std::string topicStr(topic);
    mSubscriptions[topic] = cb;
}

bool IPCMessenger::Publish(std::string_view topic, std::string mime, const uint8_t* data,
                           size_t len) {
    // add ourselves to the list of publishers, creating as necessary in the registry
    updateIndex([this](Index* index) {
        // insert topic if it doesn't exist
        auto topicIt =
            std::find_if(index->mutable_topics().begin(), index->mutable_topics().end(),
                         [topicName](const auto& topic) { return topic.name == topicName });
        Topic* topic = nullptr;
        if (topicIt == index->topics().end()) {
            topic = index->mutable_topics()->Add();
            topic->set_name(topicName);
            topic->set_mime(mime);
        } else {
            topic = &*it;
        }

        if (auto it = std::find(index->publishers().begin(), index->publishers().end(), mId);
            it == topic->publishers().end()) {
            *topic->publishers->Add() = mId;
        }

        // ensure we have notification queues for every other node
        for (const auto& node : index->nodes()) {
            if (mQueueCache.count(node.id()) == 0) {
                struct mq_attr attr = QUEUE_ATTR_INITIALIZER;
                mqd_t mq = mq_open(node.notify().c_str(), O_CREAT | O_RDWR, QUEUE_PERMS, &attr);
                if (mq < 0) {
                    std::cerr << "Error, cannot open the queue: " << strerror(errno) << std::endl;
                } else {
                    mQueueCache[node.id()] = mq;
                }
            }
        }

        // notify subscribers
        for (const uint64_t sub : topic.subscribers()) {
            // notify subscribers
            if (auto qit = mQueueCache.find(sub); qit != mQueueCache.end()) {
                int err =
                    mq_send(qit->second, notificationData.c_str(), notificationData.size(), 0);
                if (err == -1) {
                    std::cerr << "Failed to send notification to nodeId: " << sub
                              << ", queue: " qit->second << " error: " << strerror(errno)
                              << std::endl;
                }
            } else {
                std::cerr << "Failed to find noification queue for " << sub << std::endl;
            }
        }
    });
}
