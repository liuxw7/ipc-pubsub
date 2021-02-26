#include "ipc_pubsub/Subscriber.h"

#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
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

namespace ipc_pubsub {

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

Subscriber::Subscriber(std::function<void(std::shared_ptr<IPCMessage>)> callback)
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

            std::cerr << ns << std::endl;
            auto msg = IPCMessage::Create(ns, msgh);
            if (msg != nullptr) {
                mCallback(msg);
            }
        }

        // if we exit due to error, make sure to notify any blocking processes
        size_t val = UINT32_MAX;
        write(mShutdownFd, &val, sizeof(size_t));
    });
}

}  // namespace ipc_pubsub
