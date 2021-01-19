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

    void Broadcast(size_t len, uint8_t* data);

    template <typename T>
    void Broadcast(const T& msg) {
        std::unordered_set<int> clients;
        {
            std::lock_guard<std::mutex> lk(mMtx);
            clients = mClients;
        }
        for (int client : clients) msg.SerializeToFileDescriptor(client);
    }

    // send to client with the given file descriptor
    template <typename T>
    void Send(int fd, const T& msg) {
        {
            std::lock_guard<std::mutex> lk(mMtx);
            if (mClients.count(fd) == 0) return;
        }
        msg.SerializeToFileDescriptor(fd);
    }

   private:
    std::mutex mMtx;
    int mListenFd = -1;
    std::unordered_set<int> mClients;
};
