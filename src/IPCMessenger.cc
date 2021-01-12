#include "IPCMessenger.h"

#include <fcntl.h> /* For O_* constants */
#include <mqueue.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <random>

#include "protos/index.pb.h"

const int QUEUE_PERMS = static_cast<int>(0744);
const char MAGIC[8] = "MAGICPS";
const char SHMEM_NAME[] = "/ipc_pub_sub";

const int QUEUE_MAXMSG = 10;
const int QUEUE_MSGSIZE = 4096;
const mq_attr QUEUE_ATTR_INITIALIZER{
    .mq_flags = 0, .mq_maxmsg = QUEUE_MAXMSG, .mq_msgsize = QUEUE_MSGSIZE, .mq_curmsgs = 0};

struct OnReturn {
    OnReturn(std::function<void()> cleanup) : mCleanup(cleanup) {}
    ~OnReturn() { mCleanup(); }
    std::function<void()> mCleanup;
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

static bool updateIndex(int fd, std::function<bool(ipc_pubsub::Index* update)> cb) {
    flock(fd, LOCK_EX);
    OnReturn onRet([fd]() { flock(fd, LOCK_UN); });

    // read index
    ipc_pubsub::Index index;
    lseek(fd, 16, SEEK_SET);
    index.ParseFromFileDescriptor(fd);

    // allow callback to update
    return cb(&index);
}

static bool registerNode(int fd, const char* nodeName, uint64_t nodeId, const char* queueName) {
    return updateIndex(fd, [nodeName, nodeId, queueName](ipc_pubsub::Index* update) {
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

static bool unRegisterNode(int fd, uint64_t nodeId) {
    return updateIndex(fd, [nodeId](ipc_pubsub::Index* update) {
        // copy then delete old nodes
        const auto oldNodes = update->nodes();
        update->mutable_nodes()->Clear();
        for (const auto& node : oldNodes) {
            if (node.id() != nodeId) {
                // doesn't match, so copy
                *update->mutable_nodes()->Add() = node;
            }
        }

        for (auto& topic : *update->mutable_topics()) {
            // copy then delete old nodes from publishers
            const auto oldPublishers = topic.publishers();
            const auto oldSubscribers = topic.subscribers();
            topic.mutable_publishers()->Clear();
            topic.mutable_subscribers()->Clear();
            for (const uint64_t id : oldPublishers) {
                if (id != nodeId) {
                    *topic.mutable_publishers()->Add() = id;
                }
            }

            for (const uint64_t id : oldSubscribers) {
                if (id != nodeId) {
                    *topic.mutable_subscribers()->Add() = id;
                }
            }
        }
        return true;
    });
}

char intToHex[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                     '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

std::shared_ptr<IPCMessenger> IPCMessenger::Create(const char* ipcName, const char* nodeName) {
    std::random_device rd;
    std::mt19937_64 rng(rd());
    const uint64_t nodeId = rng();
    char queueName[10] = "/00000000";
    for (uint8_t i = 0; i < 8; ++i) {
        queueName[i + 1] = intToHex[(nodeId >> ((15 - i) * 4)) & 0xf];
    }

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
    OnReturn onRet([fd]() { flock(fd, LOCK_UN); });
    char magic[8];
    if (pread(fd, magic, 8, 0) == -1) {
        std::cerr << "Failed to read from shm: " << strerror(errno) << std::endl;
        return nullptr;
    }

    if (strncmp(magic, MAGIC, 8) != 0) {
        // not initialized resize and initialize
        ipc_pubsub::Index index;
        if (ftruncate(fd, 8 + 8 + index.ByteSizeLong()) == -1) {
            std::cerr << "Failed to resize: " << strerror(errno) << std::endl;
            shm_unlink(ipcName);
            return nullptr;
        }
    }

    struct mq_attr attr = QUEUE_ATTR_INITIALIZER;
    mqd_t mq = mq_open(queueName, O_CREAT | O_RDWR, QUEUE_PERMS, &attr);
    if (mq < 0) {
        std::cerr << "Error, cannot open the queue: " << strerror(errno) << " (" << errno << ")"
                  << std::endl;
        shm_unlink(ipcName);
        return nullptr;
    }

    if (!registerNode(fd, nodeName, nodeId, queueName)) {
        return nullptr;
    }
    return std::make_shared<IPCMessenger>(ipcName, fd, queueName, nodeName, nodeId, mq);
}

void IPCMessenger::onNotify(const char* data, int64_t len) {
    ipc_pubsub::NotifyMessage msg;
    msg.ParseFromArray(data, static_cast<int>(len));
}

IPCMessenger::~IPCMessenger() {
    mq_close(mNotify);
    mq_unlink(mNotifyName.c_str());
    shm_unlink(mIpcName.c_str());
}

IPCMessenger::IPCMessenger(const char* ipcName, int shmFd, const char* notifyName,
                           const char* nodeName, uint64_t nodeId, mqd_t notify)
    : mKeepGoing(true),
      mShmFd(shmFd),
      mIpcName(ipcName),
      mNotifyName(notifyName),
      mNodeName(nodeName),
      mNodeId(nodeId),
      mNotify(notify) {
    mNotifyThread = std::thread([this]() {
        unsigned int prio;
        char buffer[QUEUE_MSGSIZE + 1];
        ipc_pubsub::NotifyMessage notifyMsg;
        while (mKeepGoing) {
            memset(buffer, 0x00, sizeof(buffer));
            int64_t bytesRead = mq_receive(mNotify, buffer, QUEUE_MSGSIZE, &prio);
            if (bytesRead >= 0) {
                onNotify(buffer, bytesRead);
            }
        }
    });
}

bool IPCMessenger::Subscribe(std::string_view topicName, std::string_view mime,
                             std::function<void(const uint8_t* data, size_t len)> cb) {
    // add ourselves to the list of subscribers, creating as necessary in the registry
    bool ret = updateIndex(mShmFd, [this, topicName, mime](ipc_pubsub::Index* index) {
        // insert topic if it doesn't exist
        auto topicIt =
            std::find_if(index->mutable_topics()->begin(), index->mutable_topics()->end(),
                         [topicName](const auto& topic) { return topic.name() == topicName; });
        ipc_pubsub::Topic* topic = nullptr;
        if (topicIt == index->mutable_topics()->end()) {
            topic = index->mutable_topics()->Add();
            topic->set_name(topicName.data(), topicName.size());
            topic->set_mime(mime.data(), mime.size());
        } else {
            topic = &*topicIt;
        }

        // add ourselves to the list of subscribers
        auto subIt = std::find(topic->subscribers().begin(), topic->subscribers().end(), mNodeId);
        if (subIt == topic->subscribers().end()) {
            *topic->mutable_subscribers()->Add() = mNodeId;
        }
        return true;
    });

    if (ret) {
        std::string topicStr(topicName);
        mSubscriptions[topicStr] = cb;
        return true;
    } else {
        return false;
    }
}

bool IPCMessenger::Publish(std::string_view topicName, std::string mime, const uint8_t* data,
                           size_t len) {
    // add ourselves to the list of publishers, creating as necessary in the registry
    // TODO actually make shared mem and set data

    return updateIndex(mShmFd, [this, topicName, mime](ipc_pubsub::Index* index) {
        // insert topic if it doesn't exist
        auto topicIt =
            std::find_if(index->mutable_topics()->begin(), index->mutable_topics()->end(),
                         [topicName](const auto& topic) { return topic.name() == topicName; });
        ipc_pubsub::Topic* topic = nullptr;
        if (topicIt == index->mutable_topics()->end()) {
            topic = index->mutable_topics()->Add();
            topic->set_name(topicName.data(), topicName.size());
            topic->set_mime(mime.data(), mime.size());
        } else {
            topic = &*topicIt;
        }

        // add ourselves to the list of publishers
        if (auto it = std::find(topic->publishers().begin(), topic->publishers().end(), mNodeId);
            it == topic->publishers().end()) {
            *topic->mutable_publishers()->Add() = mNodeId;
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
        ipc_pubsub::NotifyMessage notifyMsg;
        std::string notificationData;
        notifyMsg.SerializeToString(&notificationData);

        for (const uint64_t sub : topic->subscribers()) {
            // notify subscribers
            if (auto qit = mQueueCache.find(sub); qit != mQueueCache.end()) {
                int err =
                    mq_send(qit->second, notificationData.c_str(), notificationData.size(), 0);
                if (err == -1) {
                    std::cerr << "Failed to send notification to nodeId: " << sub
                              << ", queue: " << qit->second << " error: " << strerror(errno)
                              << std::endl;
                }
            } else {
                std::cerr << "Failed to find noification queue for " << sub << std::endl;
            }
        }

        return true;
    });
}
