#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <string>

struct mqd_t;

class IPCMessenger {
   public:
    using CallbackT = std::function<void(const uint8_t* data, size_t len)>;

    // don't call directly, use Create
    IPCMessenger(const char* ipcName, int shmFd, const char* notifyName, mqd_t notify);

    static std::shared_ptr<IPCMessenger> Create(const char* ipcName, const char* nodeName);

    bool Subscribe(std::string_view topic, std::string_view mime, CallbackT cb);
    bool Publish(std::string_view topic, std::string mime, const uint8_t* data, size_t len);

   private:
    bool keepGoing = false;
    int mShmFd;
    std::unordered_map<std::string, CallbackT> mSubscriptions;
    std::unordered_map<uint64_t, int> mQueueCache;
    std::string mIpcName;
    std::string mNotifyName;
    int mNotify;
};
