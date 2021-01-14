#pragma once
#include <memory>

namespace std {
template <typename T>
class atomic;
}

class ShmBuffer {
   public:
    ShmBuffer(std::string_view shmName, int shmFd, uint64_t mapLen, void* mapped, uint64_t dataLen,
              uint8_t* data, std::atomic<int32_t>* useCount);
    ~ShmBuffer();

    size_t Size() const;
    const uint8_t* Data() const;
    uint8_t* Data();
    void SetReceiverCount(int32_t);
    int32_t RecieversRemaining();
    const std::string& Name() const;

    static std::shared_ptr<ShmBuffer> Create(size_t len);
    static std::shared_ptr<ShmBuffer> Load(std::string_view shmName);

   private:
    std::string mName;  // name of shared memory segment
    int mShmFd;         // file descriptor

    size_t mMapLen;
    void* mMapped;

    size_t mDataLen;
    uint8_t* mData;  // pointer to data in shared memory

    // reference count in shared memory, when this hits zero the *sender* is
    // responsible for unlinking, this gives the sender the power to reuse
    std::atomic<int32_t>* mRecvCount;
};
