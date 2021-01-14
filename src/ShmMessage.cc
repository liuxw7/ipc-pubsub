#include "ShmMessage.h"

#include <fcntl.h> /* For O_* constants */
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */
#include <unistd.h>

#include <atomic>
#include <cstring> /* strerror */
#include <iostream>
#include <memory>
#include <random>

#include "Utils.h"

constexpr uint32_t kBalanceSize = sizeof(std::atomic<int32_t>);

std::shared_ptr<ShmMessage> ShmMessage::Create(size_t len) {
    std::random_device rd{};
    std::mt19937_64 rng(rd());

    char shmName[18] = "/0000000000000000";
    ToHexString(rng(), shmName);

    int fd = shm_open(reinterpret_cast<const char*>(shmName), O_RDWR | O_CREAT | O_EXCL, 0755);
    if (fd < 0) {
        std::cerr << "Failed to create shared memory segment: " << shmName
                  << " error: " << strerror(errno) << std::endl;
        return nullptr;
    }

    const size_t shmSize = len + kBalanceSize;
    if (ftruncate(fd, shmSize) == -1) {
        std::cerr << "Failed to resize memory, error: " << strerror(errno) << std::endl;
        shm_unlink(reinterpret_cast<const char*>(shmName));
        return nullptr;
    }

    uint8_t* mapped = reinterpret_cast<uint8_t*>(
        mmap(nullptr, shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));

    // Initialize balance to 1
    std::atomic<int32_t>* balance = reinterpret_cast<std::atomic<int32_t>*>(mapped);
    *balance = 1;

    return std::make_shared<ShmMessage>(shmName, fd, shmSize, mapped);
}

size_t ShmMessage::Size() const { return mDataLen; }

const uint8_t* ShmMessage::Data() const { return mData; }

uint8_t* ShmMessage::Data() { return mData; }

void ShmMessage::IncBalance() { ++(*mBalance); }

const std::string& ShmMessage::Name() const { return mName; }

std::shared_ptr<ShmMessage> ShmMessage::Load(std::string_view shmName) {
    int fd = shm_open(shmName.data(), O_RDWR, 0755);
    if (fd < 0) {
        std::cerr << "Failed to open shared memory segment: " << shmName
                  << " error: " << strerror(errno) << std::endl;
        return nullptr;
    }

    off_t sz = lseek(fd, 0, SEEK_END);
    if (sz < kBalanceSize) {
        std::cerr << "Failed to seek to end of segment or segment did not contain use count: "
                  << shmName << ", error: " << strerror(errno) << std::endl;
        close(fd);
        return nullptr;
    }

    lseek(fd, 0, SEEK_SET);
    uint8_t* mapped =
        reinterpret_cast<uint8_t*>(mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    return std::make_shared<ShmMessage>(shmName, fd, sz, mapped);
}

ShmMessage::~ShmMessage() {
    std::cerr << "Deconstructing " << mName << " balance: " << mBalance->load() << std::endl;
    const bool doUnlink = (--(*mBalance) == 0);
    munmap(mMapped, mMapLen);
    close(mShmFd);
    if (doUnlink) {
        std::cerr << "Unlinking " << mName << std::endl;
        shm_unlink(mName.c_str());
    }
}

ShmMessage::ShmMessage(std::string_view shmName, int shmFd, uint64_t mapLen, uint8_t* mapped)
    : mName(shmName), mShmFd(shmFd), mMapLen(mapLen), mMapped(mapped) {
    constexpr uint32_t balanceSize = sizeof(mBalance);

    mBalance = reinterpret_cast<std::atomic<int32_t>*>(mapped);
    mData = reinterpret_cast<uint8_t*>(mapped + balanceSize);
    mDataLen = mapLen - kBalanceSize;
    std::cerr << "Constructing " << mName << " balance: " << mBalance->load() << std::endl;
}
