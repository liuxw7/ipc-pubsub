#include "ShmBuffer.h"

#include <fcntl.h> /* For O_* constants */
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */
#include <unistd.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <random>

#include "Utils.h"

std::shared_ptr<ShmBuffer> ShmBuffer::Create(size_t len) {
    std::random_device rd{};
    std::mt19937_64 rng(rd());

    char shmName[18] = "/0000000000000000";
    ToHexString(rng(), shmName);

    int fd = shm_open(reinterpret_cast<const char*>(shmName), O_RDWR | O_CREAT | O_EXCL, 0);
    if (fd < 0) {
        std::cerr << "Failed to open shared memory segment: " << shmName << std::endl;
        return nullptr;
    }

    if (ftruncate(fd, len) == -1) {
        std::cerr << "Failed to resize memory" << std::endl;
        shm_unlink(reinterpret_cast<const char*>(shmName));
        return nullptr;
    }

    uint8_t* mapped =
        reinterpret_cast<uint8_t*>(mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    std::atomic<int32_t>* recvCount = reinterpret_cast<std::atomic<int32_t>*>(mapped);
    uint8_t* data = reinterpret_cast<uint8_t*>(mapped + sizeof(*recvCount));

    *recvCount = 0;
    return std::make_shared<ShmBuffer>(shmName, fd, len, mapped, len - sizeof(*recvCount), data,
                                       recvCount);
}

size_t ShmBuffer::Size() const { return mDataLen; }

const uint8_t* ShmBuffer::Data() const { return mData; }

uint8_t* ShmBuffer::Data() { return mData; }

int32_t ShmBuffer::RecieversRemaining() { return mRecvCount->load(); }

void ShmBuffer::SetReceiverCount(int32_t value) { *mRecvCount = value; }

const std::string& ShmBuffer::Name() const { return mName; }

std::shared_ptr<ShmBuffer> ShmBuffer::Load(std::string_view shmName) {
    int fd = shm_open(shmName.data(), O_RDWR, 0);
    if (fd < 0) {
        std::cerr << "Failed to open shared memory segment: " << shmName << std::endl;
        return nullptr;
    }

    off_t sz = lseek(fd, 0, SEEK_END);
    if (sz <= 8) {
        std::cerr << "Failed to seek to end of segment or segment did not contain use count: "
                  << shmName << std::endl;
        close(fd);
        return nullptr;
    }

    lseek(fd, 0, SEEK_SET);
    uint8_t* mapped =
        reinterpret_cast<uint8_t*>(mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    std::atomic<int32_t>* recvCount = reinterpret_cast<std::atomic<int32_t>*>(mapped);
    uint8_t* data = reinterpret_cast<uint8_t*>(mapped + sizeof(*recvCount));
    return std::make_shared<ShmBuffer>(shmName, fd, sz, mapped, sz - 8, data, recvCount);
}

ShmBuffer::~ShmBuffer() {
    // now that we are done with this buffer we can say that we have done receiving it
    --(*mRecvCount);
    munmap(mMapped, mMapLen);
    close(mShmFd);
    shm_unlink(mName.c_str());
}

ShmBuffer::ShmBuffer(std::string_view shmName, int shmFd, uint64_t mapLen, void* mapped,
                     uint64_t dataLen, uint8_t* data, std::atomic<int32_t>* recvCount)
    : mName(shmName),
      mShmFd(shmFd),
      mMapLen(mapLen),
      mMapped(mapped),
      mDataLen(dataLen),
      mData(data),
      mRecvCount(recvCount) {}
