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

std::shared_ptr<UDSClient> UDSClient::Create(std::string_view sockPath, OnDataCallback onData,
                                             std::function<void()> onDisconnect) {
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
        // SPDLOG_ERROR("failed to connect to {}, error: {}", sockPath, strerror(errno));
        close(fd);
        return nullptr;
    }

    int shutdownFd = eventfd(0, EFD_SEMAPHORE);
    if (shutdownFd == -1) {
        perror("Failed to create event!");
        return nullptr;
    }

    SPDLOG_INFO("Connected with {}", fd);
    return std::make_shared<UDSClient>(fd, shutdownFd, onData, onDisconnect);
}

UDSClient::UDSClient(int fd, int shutdownFd, OnDataCallback onData,
                     std::function<void()> onDisconnect)
    : mOnData(onData), mOnDisconnect(onDisconnect), mFd(fd), mShutdownFd(shutdownFd) {
    mMainThread = std::thread([this]() { MainLoop(); });
}

UDSClient::~UDSClient() {
    Shutdown();
    Wait();
    mMainThread.join();
}

void UDSClient::Shutdown() {
    std::scoped_lock<std::mutex> lk(mMtx);
    if (mShutdownFd != -1) {
        // Since this is a semaphore type there could be up to UINT32_MAX Wait()
        // calls running simultaneously before we run into issues
        // The only reason I didn't use UINT64_MAX was because I am worried about
        // rollovers
        uint64_t data = UINT32_MAX;
        write(mShutdownFd, &data, sizeof(data));
    }
}

void UDSClient::Wait() {
    struct pollfd fds[1];
    {
        std::scoped_lock<std::mutex> lk(mMtx);
        if (mShutdownFd == -1) {
            return;
        }
        fds[0].fd = mShutdownFd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;
    }

    // wait for shutdown signal, we don't particularly care if this fails (for instance if
    // mShutdownFd gets closed in between the locked section and here), the point
    // is if it is still valid to block until its triggered
    poll(fds, 1, -1);
}

void UDSClient::MainLoop() {
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
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            SPDLOG_ERROR("Failed to Poll: {}", strerror(errno));
            break;
        }

        if (fds[0].revents != 0) {
            SPDLOG_INFO("Polled shutdown");
            // shutdown event received, exit
            SPDLOG_ERROR("UDSClient shutdown");
            break;
        }

        if (fds[1].revents != 0) {
            if (fds[1].revents & POLLIN) {
                // socket has data, read it
                uint8_t buffer[UINT16_MAX];
                SPDLOG_INFO("onData");
                int64_t nBytes = read(mFd, buffer, UINT16_MAX);
                if (nBytes == -1) {
                    strerror_r(errno, reinterpret_cast<char*>(buffer), UINT16_MAX);
                    SPDLOG_ERROR("Error reading: '{}'", buffer);
                } else if (nBytes == 0) {
                    SPDLOG_ERROR("Empty");
                } else if (mOnData) {
                    mOnData(nBytes, buffer);
                }
            }
            if (fds[1].revents & POLLERR) {
                SPDLOG_ERROR("error");
                break;
            }
            if (fds[1].revents & POLLHUP) {
                // server shutdown
                SPDLOG_INFO("Server shutdown");
                break;
            }
            if (fds[1].revents & POLLNVAL) {
                SPDLOG_INFO("File descriptor not open");
                break;
            }
        }
    }

    SPDLOG_INFO("Exiting UDSClient::MainLoop");
    if (mOnDisconnect) mOnDisconnect();

    // ensure Wait knows we are killing this
    uint64_t data = UINT32_MAX;
    write(mShutdownFd, &data, sizeof(data));

    std::scoped_lock<std::mutex> lk(mMtx);
    close(mFd);
    close(mShutdownFd);
    mFd = -1;
    mShutdownFd = -1;
}

// send to client with the given file descriptor
int64_t UDSClient::Send(size_t len, uint8_t* message) {
    {
        std::scoped_lock<std::mutex> lk(mMtx);
        if (mFd == -1) {
            SPDLOG_ERROR("Attempting to write to shutdown UDSClient");
            return -1;
        }
    }

    int64_t ret = send(mFd, message, len, MSG_EOR);
    return ret;
}
