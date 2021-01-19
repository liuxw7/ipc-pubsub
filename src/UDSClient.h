#pragma once
#include <functional>
#include <memory>

class UDSClient {
   public:
    static std::shared_ptr<UDSClient> Create(std::string_view sockPath);
    UDSClient(int fd);

    int LoopUntilShutdown(int shutdownFd, std::function<void(size_t, uint8_t*)> onData);

    // send to client with the given file descriptor
    int64_t Send(size_t len, uint8_t* message);
    template <typename T>
    int64_t Send(const T& msg) {
        bool ret = msg.SerializeToFileDescriptor(mFd);
        if (ret) {
            return msg.ByteSizeLong();
        } else {
            return -1;
        }
    }

   private:
    int mFd = -1;
};
