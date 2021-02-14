#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cassert>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

struct Message {
    static std::shared_ptr<Message> Create(struct msghdr msgh);
    Message(std::string_view meta, uint8_t* ptr, size_t len, int fd)
        : metaData(meta), blobPtr(ptr), blobSize(len), mFd(fd) {}

    ~Message() {
        munmap(blobPtr, blobSize);
        if (mFd != -1) close(mFd);
    }

    std::string_view Contents() {
        return std::string_view(reinterpret_cast<char*>(blobPtr), blobSize);
    }
    std::string_view MetaData() { return std::string_view(metaData); }

    // small amount of data informing the meaning of the larger blob
    std::string metaData;

    // large chunk of memory mapped / shared memory data
    uint8_t* blobPtr;
    size_t blobSize;

    int mFd = -1;
};

std::shared_ptr<Message> Message::Create(struct msghdr msgh) {
    int recvFd;
    struct cmsghdr* cmsgp = CMSG_FIRSTHDR(&msgh);
    if (cmsgp == nullptr || cmsgp->cmsg_len != CMSG_LEN(sizeof(recvFd))) {
        std::cerr << "bad cmsg header / message length" << std::endl;
        return nullptr;
    }
    if (cmsgp->cmsg_level != SOL_SOCKET) {
        std::cerr << "cmsg_level != SOL_SOCKET" << std::endl;
        return nullptr;
    }
    if (cmsgp->cmsg_type != SCM_RIGHTS) {
        std::cerr << "cmsg_type != SCM_RIGHTS" << std::endl;
        return nullptr;
    }

    if (msgh.msg_iovlen == 0) {
        std::cerr << "No vector data!?" << std::endl;
        return nullptr;
    }
    if (msgh.msg_iovlen != 1) {
        std::cerr << "Expected exactly 1 pieces of vector data, others will be ignoed" << std::endl;
        return nullptr;
    }

    // construct metadata from first vector payload
    std::cerr << msgh.msg_iov[0].iov_len << std::endl;
    std::string_view meta(reinterpret_cast<const char*>(msgh.msg_iov[0].iov_base),
                          msgh.msg_iov[0].iov_len);

    /* The data area of the 'cmsghdr' is an 'int' (a file descriptor);
       copy that integer to a local variable. (The received file descriptor
       is typically a different file descriptor number than was used in the
       sending process.) */
    memcpy(&recvFd, CMSG_DATA(cmsgp), sizeof(recvFd));

    // seek to end so we know the length of the file to map
    ssize_t blobLen = lseek(recvFd, 0, SEEK_END);
    if (blobLen < 0) {
        perror("Failed to see to end of received file descriptor");
        return nullptr;
    }

    void* mappedData = mmap(nullptr, blobLen, PROT_READ, MAP_PRIVATE, recvFd, 0);
    if (mappedData == nullptr) {
        perror("Failed to map");
        return nullptr;
    }

    return std::make_shared<Message>(meta, reinterpret_cast<uint8_t*>(mappedData), blobLen, recvFd);
}

class Subscriber {
   public:
    Subscriber(std::function<void(std::shared_ptr<Message>)> callback);
    ~Subscriber();
    void WaitForShutdown();

    int mFd;
    std::thread mReadThread;
    std::function<void(std::shared_ptr<Message>)> mCallback;

    int mShutdownFd = -1;
};

Subscriber::~Subscriber() {
    // trigger shutdown
    size_t val = UINT32_MAX;
    write(mShutdownFd, &val, sizeof(size_t));

    mReadThread.join();
}

void Subscriber::WaitForShutdown() {
    size_t data;
    read(mShutdownFd, &data, sizeof(data));
}

Subscriber::Subscriber(std::function<void(std::shared_ptr<Message>)> callback)
    : mCallback(callback) {
    mShutdownFd = eventfd(0, EFD_SEMAPHORE);

    struct sockaddr_un addr;
    const std::string socket_path = "\0socket";
    assert(socket_path.size() + 1 < sizeof(addr.sun_path));

    if ((mFd = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1) {
        perror("socket error");
        exit(-1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::copy(socket_path.c_str(), socket_path.c_str() + socket_path.size(), addr.sun_path);
    if (connect(mFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        perror("connect error");
        exit(-1);
    }

    mReadThread = std::thread([this]() {
        struct msghdr msgh;

        msgh.msg_name = nullptr;  // doesn't matter because we are connected
        msgh.msg_namelen = 0;

        constexpr size_t BUFFLEN = 1 << 12;
        char buffer[BUFFLEN];
        struct iovec iov;
        msgh.msg_iov = &iov;
        msgh.msg_iovlen = 1;
        iov.iov_base = buffer;
        iov.iov_len = BUFFLEN;

        int recvFd = -1;
        union {
            char buf[CMSG_SPACE(sizeof(recvFd))];
            /* Space large enough to hold an 'int' */
            struct cmsghdr align;
        } controlMsg;
        msgh.msg_control = controlMsg.buf;
        msgh.msg_controllen = sizeof(controlMsg.buf);

        while (ssize_t ns = recvmsg(mFd, &msgh, 0)) {
            if (ns < 0) {
                perror("Bad recv");
                return;
            }

            auto msg = Message::Create(msgh);
            if (msg != nullptr) {
                mCallback(msg);
            }
        }

        // if we exit due to error, make sure to notify any blocking processes
        size_t val = UINT32_MAX;
        write(mShutdownFd, &val, sizeof(size_t));
    });
}

static void Callback(std::shared_ptr<Message> msg) {
    std::cerr << "Message Received" << std::endl;
    std::cerr << "Meta: " << msg->MetaData() << std::endl;
    std::cerr << "Msg: " << msg->Contents() << std::endl;
}

int main(int argc, char** argv) {
    Subscriber sub(Callback);
    sub.WaitForShutdown();
    return 0;
}
