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

   private:
    void OnNotify();
    sem_t* GetSem(const std::string& semName);

    std::atomic_bool mKeepGoing = false;
    const int mShmFd;

    // Data structure for current subscriptions and callback thread
    Dispatcher mDispatcher;

    // Used to notify other nodes
    std::mutex mSemMtx;  // lock for sem cache
    std::unordered_map<std::string, sem_t*> mSemCache;

    // metadata IPC
    const std::string mIpcName;

    const std::string mNodeName;
    const uint64_t mNodeId;

    // Wait for semaphore then reload / dispatch
    const std::string mNotifyName;
    sem_t* mNotify;
    std::thread mNotifyThread;
};
