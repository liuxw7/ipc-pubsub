#pragma once
#include <memory>

namespace std {
template <typename T>
class atomic;
}

/*
 * Wraps a shared memory segment containining a message and reception count.
 * Once remaining receptions reaches zero the messages is deallocated
 */
class ShmMessage {
   public:
    ShmMessage(std::string_view shmName, int shmFd, uint64_t mapLen, uint8_t* mapped);
    ShmMessage(const ShmMessage& other) = delete;
    ~ShmMessage();

    /* Size of Payload */
    size_t Size() const;

    /* Payload */
    const uint8_t* Data() const;
    uint8_t* Data();

    // Add additional Receiver (typically called at the sender)
    void IncBalance();

    // Shared Memory Segment Name (usually random)
    const std::string& Name() const;

    /*
     * Constructors a new ShmMessage
     */
    static std::shared_ptr<ShmMessage> Create(size_t len);

    /*
     * Given a shared memory segment name construct a new ShmMessage wrapper
     */
    static std::shared_ptr<ShmMessage> Load(std::string_view shmName);

   private:
    std::string mName;  // name of shared memory segment
    int mShmFd;         // file descriptor

    size_t mMapLen;
    void* mMapped;

    size_t mDataLen;
    uint8_t* mData;  // pointer to data in shared memory

    // reference count in shared memory, when this hits zero the *sender* is
    // responsible for unlinking, this gives the sender the power to reuse
    std::atomic<int32_t>* mBalance;
};
