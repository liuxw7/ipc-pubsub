#pragma once
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

struct msghdr;

namespace ipc_pubsub {
struct IPCMessage {
   public:
    static std::shared_ptr<IPCMessage> Create(size_t vecLen, const struct msghdr& msgh);
    IPCMessage(std::string_view meta, uint8_t* ptr, size_t len, int fd)
        : metaData(meta), blobPtr(ptr), blobSize(len), mFd(fd) {}
    ~IPCMessage();

    const std::string_view Contents() const {
        return std::string_view(reinterpret_cast<char*>(blobPtr), blobSize);
    }
    const std::string_view MetaData() const { return std::string_view(metaData); }

   private:
    // small amount of data informing the meaning of the larger blob
    std::string metaData;

    // large chunk of memory mapped / shared memory data
    uint8_t* blobPtr;
    size_t blobSize;

    // File Descriptor of Shared Memory
    int mFd = -1;
};

class Subscriber {
   public:
    Subscriber(std::function<void(std::shared_ptr<IPCMessage>)> callback);
    ~Subscriber();
    void WaitForShutdown();

    int mFd;
    std::thread mReadThread;
    std::function<void(std::shared_ptr<IPCMessage>)> mCallback;

    int mShutdownFd = -1;
};
}  // namespace ipc_pubsub
