#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace ips {
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
    void MainLoop();

    // Clients and filedescriptors only get changed on
    std::mutex mMtx;
    std::unordered_set<int> mClients;
    int mListenFd = -1;
    int mShutdownFd = -1;

    const ConnHandler mOnConnect;
    const ConnHandler mOnDisconnect;
    const DataHandler mOnData;
    std::thread mMainThread;
};
}  // namespace ips
