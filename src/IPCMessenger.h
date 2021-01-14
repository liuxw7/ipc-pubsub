#pragma once
#include <semaphore.h>

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "Dispatcher.h"

class IPCMessenger : std::enable_shared_from_this<IPCMessenger> {
   public:
    using CallbackT = std::function<void(const uint8_t* data, size_t len)>;

    // don't call directly, use Create
    IPCMessenger(const char* ipcName, int shmFd, const char* notifyName, const char* nodeName,
                 uint64_t nodeId, sem_t* notify);
    ~IPCMessenger();

    static std::shared_ptr<IPCMessenger> Create(const char* ipcName, const char* nodeName);

    bool Subscribe(std::string_view topic, CallbackT cb);
    bool Publish(std::string_view topic, const uint8_t* data, size_t len);
    bool Publish(std::string_view topic, std::shared_ptr<ShmMessage> buff);
    bool Announce(std::string_view topic, std::string_view mime);

    // Don't call this unless you know what you are doing
    void Shutdown();

   private:
    void OnNotify();

    bool mKeepGoing = false;
    int mShmFd;

    // Data structure for current subscriptions and callback thread
    Dispatcher mDispatcher;

    // Used to notify other nodes
    std::unordered_map<uint64_t, sem_t*> mSemCache;

    // metadata IPC
    std::string mIpcName;

    std::string mNodeName;
    uint64_t mNodeId;

    // Wait for semaphore then reload / dispatch
    std::string mNotifyName;
    sem_t* mNotify;
    std::thread mNotifyThread;
};
