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
        : mMetaData(meta), mBlobPtr(ptr), mBlobSize(len), mFd(fd) {}
    ~IPCMessage();

    const std::string_view Contents() const {
        return std::string_view(reinterpret_cast<char*>(mBlobPtr), mBlobSize);
    }
    const std::string_view MetaData() const { return std::string_view(mMetaData); }

   private:
    // small amount of data informing the meaning of the larger blob
    std::string mMetaData;

    // large chunk of memory mapped / shared memory data
    uint8_t* mBlobPtr;
    size_t mBlobSize;

    // File Descriptor of Shared Memory
    int mFd = -1;
};
}  // namespace ipc_pubsub
