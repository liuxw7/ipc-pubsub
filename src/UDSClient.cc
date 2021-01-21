#include "UDSClient.h"

#include <poll.h>
#include <spdlog/spdlog.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cassert>
#include <functional>
#include <memory>
#include <string_view>

#include "Utils.h"

std::shared_ptr<UDSClient> UDSClient::Create(std::string_view sockPath, OnDataCallback onData) {
    struct sockaddr_un addr;
    assert(!sockPath.empty());
    assert(sockPath.size() + 1 < sizeof(addr.sun_path));

    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd == -1) {
        SPDLOG_ERROR("failed to create socket: {}", strerror(errno));
        return nullptr;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::copy(sockPath.begin(), sockPath.end(), addr.sun_path);
    addr.sun_path[sockPath.size()] = 0;

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        SPDLOG_ERROR("failed to connect to {}, error: {}", sockPath, strerror(errno));
        close(fd);
        return nullptr;
    }

    int shutdownFd = eventfd(0, EFD_SEMAPHORE);
    if (shutdownFd == -1) {
        perror("Failed to create event!");
        return nullptr;
    }

    SPDLOG_INFO("Connected with {}", fd);
    return std::make_shared<UDSClient>(fd, shutdownFd, onData);
}

UDSClient::UDSClient(int fd, int shutdownFd, OnDataCallback onData)
    : mFd(fd), mShutdownFd(shutdownFd), mOnData(onData) {
    mMainThread = std::thread([this]() { LoopUntilShutdown(); });
}

UDSClient::~UDSClient() {
    Shutdown();
    Wait();
}

void UDSClient::Shutdown() {
    uint64_t data = 1;
    write(mShutdownFd, &data, sizeof(data));
}

void UDSClient::Wait() { mMainThread.join(); }

int UDSClient::LoopUntilShutdown() {
    struct pollfd fds[2];
    fds[0].fd = mShutdownFd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    fds[1].fd = mFd;
    fds[1].events = POLLIN;
    fds[1].revents = 0;
    // now that we are connected field events from leader OR shutdown event
    // wait for it to close or shutdown event
    while (1) {
        SPDLOG_INFO("Begin poll, {}", mFd);
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            SPDLOG_ERROR("Failed to Poll: {}", strerror(errno));
            return -1;
        }
        SPDLOG_INFO("Polled [1] {:x} [2] {:x}", fds[0].revents, fds[1].revents);

        if (fds[0].revents != 0) {
            SPDLOG_INFO("Polled shutdown");
            // shutdown event received, exit
            SPDLOG_ERROR("UDSClient shutdown");
            return 0;
        }

        if (fds[1].revents != 0) {
            SPDLOG_INFO("Polled input: {:x}", fds[1].revents);
            if (fds[1].revents & POLLERR) {
                SPDLOG_ERROR("error");
                return -1;
            } else if (fds[1].revents & POLLHUP) {
                // server shutdown
                SPDLOG_INFO("Server shutdown");
                return -1;
            } else if (fds[1].revents & POLLNVAL) {
                SPDLOG_INFO("File descriptor not open");
                return -1;
            } else if (fds[1].revents & POLLIN) {
                // socket has data, read it
                uint8_t buffer[1024];
                SPDLOG_INFO("onData");
                int64_t nBytes = read(mFd, buffer, 1024);
                if (nBytes < 0) {
                    SPDLOG_ERROR("Error reading: {}", strerror(errno));
                } else {
                    if (mOnData) mOnData(nBytes, buffer);
                }
            }
        }
    }
}

// send to client with the given file descriptor
int64_t UDSClient::Send(size_t len, uint8_t* message) {
    SPDLOG_INFO("Writing {} bytes to fd: {}", len, mFd);
    int64_t ret = send(mFd, message, len, MSG_EOR);
    SPDLOG_INFO("Wrote {} bytes", ret);
    if (ret == -1) {
        SPDLOG_ERROR("Failed to send: {}", strerror(errno));
    } else {
        return ret;
    }
}
