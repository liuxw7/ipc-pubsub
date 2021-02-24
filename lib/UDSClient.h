#pragma once
#include <spdlog/spdlog.h>

#include <functional>
#include <memory>
#include <thread>

namespace ips {
class UDSClient {
   public:
    using OnDataCallback = std::function<void(int64_t, uint8_t*)>;
    static std::shared_ptr<UDSClient> Create(std::string_view sockPath,
                                             OnDataCallback onData = nullptr,
                                             std::function<void()> onDisconnect = nullptr);
    UDSClient(int fd, int shutdownFd, OnDataCallback onData = nullptr,
              std::function<void()> onDisconnect = nullptr);
    ~UDSClient();

    void Wait();
    void Shutdown();

    // send to client with the given file descriptor
    int64_t Send(size_t len, uint8_t* message);

    template <typename T>
    int64_t Send(const T& msg) {
        SPDLOG_INFO("Sending description: {}", msg.DebugString());
        thread_local std::string data;
        msg.SerializeToString(&data);
        return Send(data.size(), reinterpret_cast<uint8_t*>(data.data()));
    }

   private:
    // MainThread calls MainLoop()
    void MainLoop();

    const std::function<void(int64_t, uint8_t*)> mOnData;

    // Called at end of MainLoop
    const std::function<void()> mOnDisconnect;

    // Closed at end of MainLoop, once shutdown mFd will be -1
    std::mutex mMtx;
    int mFd;
    int mShutdownFd;

    std::thread mMainThread;
};
}  // namespace ips
