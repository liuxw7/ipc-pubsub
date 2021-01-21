#include "UDSServer.h"

#include <poll.h>
#include <spdlog/spdlog.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cassert>
#include <mutex>

#include "Utils.h"

std::shared_ptr<UDSServer> UDSServer::Create(std::string_view sockPath, ConnHandler onConnect,
                                             ConnHandler onDisconnect, DataHandler onData) {
    struct sockaddr_un addr;
    assert(!sockPath.empty());
    assert(sockPath.size() + 1 < sizeof(addr.sun_path));

    int sockFd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (sockFd == -1) {
        perror("socket error");
        return nullptr;
    }

    // wait until we become the leader (can take ownership of the socketPath)
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::copy(sockPath.begin(), sockPath.end(), addr.sun_path);
    addr.sun_path[sockPath.size()] = 0;
    if (bind(sockFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        // fail
        perror("Bind error");
        close(sockFd);
        return nullptr;
    }

    // once we have reached this point we are the leader, accept connections and
    // relay messages back out,
    // NOTE: once we become leader you might think we need a warm topology,but
    // all the known clients should immediately try to connect to us, so we'll
    // rebuild the topology from that
    if (listen(sockFd, 10) == -1) {
        perror("listen error");
        close(sockFd);
        return nullptr;
    }

    int shutdownFd = eventfd(0, EFD_SEMAPHORE);
    if (shutdownFd == -1) {
        perror("Failed to create event!");
        return nullptr;
    }

    return std::make_shared<UDSServer>(sockFd, shutdownFd, onConnect, onDisconnect, onData);
}

UDSServer::UDSServer(int fd, int shutdownFd, ConnHandler onConnect, ConnHandler onDisconnect,
                     DataHandler onData)
    : mListenFd(fd),
      mShutdownFd(shutdownFd),
      mOnConnect(onConnect),
      mOnDisconnect(onDisconnect),
      mOnData(onData) {
    mMainThread = std::thread([this]() { LoopUntilShutdown(); });
}

UDSServer::~UDSServer() {
    Shutdown();
    Wait();
}

void UDSServer::Shutdown() {
    uint64_t data = 1;
    write(mShutdownFd, &data, sizeof(data));
}

void UDSServer::Wait() { mMainThread.join(); }

int UDSServer::LoopUntilShutdown() {
    std::vector<pollfd> pollFds(2);
    pollFds[0].fd = mShutdownFd;
    pollFds[0].events = POLLIN;

    pollFds[1].fd = mListenFd;
    pollFds[1].events = POLLIN;

    OnRet onRet([this] {
        std::lock_guard<std::mutex> lk(mMtx);
        for (int fd : mClients) {
            close(fd);
        }
        mClients.clear();
    });

    while (true) {
        pollFds.resize(2);
        {
            std::lock_guard<std::mutex> lk(mMtx);
            for (int fd : mClients) {
                pollFds.push_back({.fd = fd, .events = POLLIN});
            }
        }

        // read client file descriptors
        SPDLOG_INFO("Begin poll {} fds", pollFds.size());
        if (int ret = poll(pollFds.data(), pollFds.size(), -1); ret < 0) {
            perror("Failed to poll");
            return -1;
        }
        SPDLOG_INFO("Polled [1] {:x} [2] {:x}", pollFds[0].revents, pollFds[1].revents);

        if (pollFds[0].revents != 0) {
            // shutdown event received, exit
            return 0;
        }

        if (pollFds[1].revents != 0) {
            // new client knocking
            int newClientFd = accept(mListenFd, nullptr, nullptr);
            if (newClientFd == -1) {
                perror("accept error");
                return -1;
            } else {
                std::lock_guard<std::mutex> lk(mMtx);
                mClients.emplace(newClientFd);
                if (mOnConnect) mOnConnect(newClientFd);
            }
        }

        for (size_t i = 2; i < pollFds.size(); ++i) {
            if (pollFds[i].revents == 0) {
                // no event for the filedescriptor
                SPDLOG_INFO("Empty poll");
                continue;
            }

            SPDLOG_INFO("Polled input: {:x}", pollFds[i].revents);
            if ((pollFds[i].revents & POLLIN) != 0) {
                uint8_t buffer[UINT16_MAX];
                int64_t nBytes = read(pollFds[i].fd, buffer, UINT16_MAX);
                if (mOnData) mOnData(pollFds[i].fd, nBytes, buffer);
            }
            if ((pollFds[i].revents & (POLLHUP | POLLRDHUP)) != 0) {
                if (mOnDisconnect) mOnDisconnect(pollFds[i].fd);
                std::lock_guard<std::mutex> lk(mMtx);
                mClients.erase(pollFds[i].fd);
            }
        }
    }
}
