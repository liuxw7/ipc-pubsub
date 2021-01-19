#include "ShmMessage.h"

#include <fcntl.h> /* For O_* constants */
#include <sys/mman.h>
#include <sys/stat.h> /* For mode constants */
#include <unistd.h>

#include <cstring> /* strerror */
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>

#include "Globals.h"
#include "Utils.h"

std::shared_ptr<ShmMessage> ShmMessage::Create(size_t len) {
    std::random_device rd{};
    std::mt19937_64 rng(rd());

    std::ostringstream oss;
    oss << "/" << std::fixed << std::setw(16) << std::setfill('0') << std::hex << rng();
    const std::string& shmName = oss.str();

    std::cerr << "Creating" << shmName << std::endl;
    int fd = shm_open(shmName.c_str(), O_RDWR | O_CREAT | O_EXCL, 0755);
    if (fd < 0) {
        std::cerr << "Failed to create shared memory segment: " << shmName
                  << " error: " << strerror(errno) << std::endl;
        return nullptr;
    }

    const size_t shmSize = len + sizeof(CountType);
    if (ftruncate(fd, shmSize) == -1) {
        std::cerr << "Failed to resize memory, error: " << strerror(errno) << std::endl;
        shm_unlink(shmName.c_str());
        return nullptr;
    }

    uint8_t* mapped = reinterpret_cast<uint8_t*>(
        mmap(nullptr, shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));

    // Initialize balance to 1
    int32_t* balance = reinterpret_cast<int32_t*>(mapped);
    *balance = 1;

    auto ptr = std::make_shared<ShmMessage>(shmName, fd, shmSize, mapped);
    EnsureCleanup(ptr);
    return ptr;
}

size_t ShmMessage::Size() const { return mDataLen; }

const uint8_t* ShmMessage::Data() const { return mData; }

uint8_t* ShmMessage::Data() { return mData; }

void ShmMessage::IncBalance() {
    lseek(mShmFd, 0, SEEK_SET);
    lockf(mShmFd, F_LOCK, sizeof(CountType));
    ++(*mBalance);
    lockf(mShmFd, F_ULOCK, sizeof(CountType));
}

const std::string& ShmMessage::Name() const { return mName; }

std::shared_ptr<ShmMessage> ShmMessage::Load(std::string_view shmName) {
    std::cerr << "Opening" << shmName << std::endl;
    int fd = shm_open(shmName.data(), O_RDWR, 0755);
    if (fd < 0) {
        std::cerr << "Failed to open shared memory segment: " << shmName
                  << " error: " << strerror(errno) << std::endl;
        return nullptr;
    }

    off_t sz = lseek(fd, 0, SEEK_END);
    if (sz < sizeof(CountType)) {
        std::cerr << "Failed to seek to end of segment or segment did not contain use count: "
                  << shmName << ", error: " << strerror(errno) << std::endl;
        close(fd);
        return nullptr;
    }

    lseek(fd, 0, SEEK_SET);
    uint8_t* mapped =
        reinterpret_cast<uint8_t*>(mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));

    auto ptr = std::make_shared<ShmMessage>(shmName, fd, sz, mapped);
    EnsureCleanup(ptr);
    return ptr;
}

ShmMessage::~ShmMessage() {
    // std::cerr << "Deconstructing " << mName << " balance: " << mBalance->load() << std::endl;
    lseek(mShmFd, 0, SEEK_SET);
    lockf(mShmFd, F_LOCK, sizeof(CountType));
    const bool doUnlink = --(*mBalance) == 0;
    lockf(mShmFd, F_ULOCK, sizeof(CountType));

    munmap(mMapped, mMapLen);
    close(mShmFd);
    std::cerr << "Closing " << mName << std::endl;
    if (doUnlink) {
        std::cerr << "Unlinking " << mName << std::endl;
        shm_unlink(mName.c_str());
    }
}

ShmMessage::ShmMessage(std::string_view shmName, int shmFd, uint64_t mapLen, uint8_t* mapped)
    : mName(shmName), mShmFd(shmFd), mMapLen(mapLen), mMapped(mapped) {
    mBalance = reinterpret_cast<CountType*>(mapped);
    mData = reinterpret_cast<uint8_t*>(mapped + sizeof(CountType));
    mDataLen = mapLen - sizeof(CountType);
}
