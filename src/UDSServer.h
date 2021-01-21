#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

class UDSServer {
   public:
    using ConnHandler = std::function<void(int)>;
    using DataHandler = std::function<void(int, int64_t, uint8_t*)>;
    static std::shared_ptr<UDSServer> Create(std::string_view sockPath,
                                             ConnHandler onConnect = nullptr,
                                             ConnHandler onDisconnect = nullptr,
                                             DataHandler onData = nullptr);
    UDSServer(int fd, int shutdownFd, ConnHandler onConnect, ConnHandler onDisconnect,
              DataHandler onData);

    ~UDSServer();
    void Shutdown();
    void Wait();

   private:
    int LoopUntilShutdown();
    std::mutex mMtx;
    int mListenFd = -1;
    std::unordered_set<int> mClients;
    int mShutdownFd = -1;
    ConnHandler mOnConnect;
    ConnHandler mOnDisconnect;
    DataHandler mOnData;

    std::thread mMainThread;
};
