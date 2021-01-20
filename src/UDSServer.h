#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_set>

class UDSServer {
   public:
    static std::shared_ptr<UDSServer> Create(std::string_view sockPath);
    UDSServer(int fd);

    int LoopUntilShutdown(int shutdownFd, std::function<void(int)> onConnect,
                          std::function<void(int)> onDisconnect,
                          std::function<void(int, size_t, uint8_t*)> onData);

   private:
    std::mutex mMtx;
    int mListenFd = -1;
    std::unordered_set<int> mClients;
};
