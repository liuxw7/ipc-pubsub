#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

typedef int mqd_t;

class IPCMessenger {
   public:
    using CallbackT = std::function<void(const uint8_t* data, size_t len)>;

    // don't call directly, use Create
    IPCMessenger(const char* ipcName, int shmFd, const char* notifyName, const char* nodeName,
                 uint64_t nodeId, mqd_t notify);
    ~IPCMessenger();

    static std::shared_ptr<IPCMessenger> Create(const char* ipcName, const char* nodeName);

    bool Subscribe(std::string_view topic, std::string_view mime, CallbackT cb);
    bool Publish(std::string_view topic, std::string_view mime, const uint8_t* data, size_t len);

   private:
    void onNotify(const char* data, int64_t len);

    bool mKeepGoing = false;
    int mShmFd;
    std::unordered_map<std::string, CallbackT> mSubscriptions;
    std::unordered_map<uint64_t, int> mQueueCache;
    std::string mIpcName;
    std::string mNotifyName;
    std::string mNodeName;
    uint64_t mNodeId;
    int mNotify;

    std::thread mNotifyThread;
};
