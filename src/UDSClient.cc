#include "UDSClient.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cassert>
#include <functional>
#include <memory>
#include <string_view>

#include "Utils.h"

std::shared_ptr<UDSClient> UDSClient::Create(std::string_view sockPath) {
    struct sockaddr_un addr;
    assert(!sockPath.empty());
    assert(sockPath.size() + 1 < sizeof(addr.sun_path));

    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd == -1) {
        perror("socket error");
        return nullptr;
    }

    OnRet onRet2([fd]() { close(fd); });

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::copy(sockPath.begin(), sockPath.end(), addr.sun_path);
    addr.sun_path[sockPath.size()] = 0;

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        perror("connect error");
        return nullptr;
    }

    return std::make_shared<UDSClient>(fd);
}

UDSClient::UDSClient(int fd) : mFd(fd) {}

int UDSClient::LoopUntilShutdown(int shutdownEventFd,
                                 std::function<void(size_t, uint8_t*)> onData) {
    struct pollfd fds[2];
    fds[0].fd = shutdownEventFd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    fds[1].fd = mFd;
    fds[1].events = POLLIN | POLLRDHUP | POLLHUP;
    fds[1].revents = 0;
    // now that we are connected field events from leader OR shutdown event
    // wait for it to close or shutdown event
    while (1) {
        int ret = poll(&fds[0], 2, 0);
        if (ret < 0) {
            perror("Failed to poll");
            return -1;
        }

        if (fds[0].revents != 0) {
            // shutdown event received, exit
            return 0;
        }

        if (fds[1].revents & (POLLHUP | POLLRDHUP)) {
            // server shutdown
            return 1;
        } else if (fds[0].revents & POLLIN) {
            // socket has data, read it
            uint8_t buffer[UINT16_MAX];
            int64_t nBytes = read(mFd, buffer, UINT16_MAX);
            onData(nBytes, buffer);
        }
    }
}

// send to client with the given file descriptor
int64_t UDSClient::Send(size_t len, uint8_t* message) { return write(mFd, message, len); }
