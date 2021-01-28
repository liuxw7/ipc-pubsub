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
    mMainThread = std::thread([this]() { MainLoop(); });
}

UDSServer::~UDSServer() {
    Shutdown();
    Wait();
    mMainThread.join();
}

void UDSServer::Shutdown() {
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

void UDSServer::Wait() {
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
    // mShutdownFd gets closed / set to -1 in between the locked section and here), the point
    // is if it is still valid to block until its triggered
    poll(fds, 1, -1);
}

void UDSServer::MainLoop() {
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
        if (int ret = poll(pollFds.data(), pollFds.size(), -1); ret < 0) {
            perror("Failed to poll");
            break;
        }

        if (pollFds[0].revents != 0) {
            // shutdown event received, exit
            break;
        }

        if (pollFds[1].revents != 0) {
            // new client knocking
            int newClientFd = accept(mListenFd, nullptr, nullptr);
            if (newClientFd == -1) {
                perror("accept error");
                break;
            } else {
                {
                    std::lock_guard<std::mutex> lk(mMtx);
                    mClients.emplace(newClientFd);
                }
                if (mOnConnect) mOnConnect(newClientFd);
            }
        }

        for (size_t i = 2; i < pollFds.size(); ++i) {
            if (pollFds[i].revents == 0) {
                // no event for the filedescriptor
                continue;
            }

            if ((pollFds[i].revents & POLLIN) != 0) {
                uint8_t buffer[UINT16_MAX];
                int64_t nBytes = read(pollFds[i].fd, buffer, UINT16_MAX);
                if (nBytes == -1) {
                    strerror_r(errno, reinterpret_cast<char*>(buffer), UINT16_MAX);
                    SPDLOG_ERROR("Error reading: '{}'", buffer);
                } else if (nBytes == 0) {
                    SPDLOG_ERROR("Empty");
                } else if (mOnData) {
                    mOnData(pollFds[i].fd, nBytes, buffer);
                }
            }
            if ((pollFds[i].revents & (POLLHUP | POLLRDHUP)) != 0) {
                if (mOnDisconnect) mOnDisconnect(pollFds[i].fd);
                std::lock_guard<std::mutex> lk(mMtx);
                mClients.erase(pollFds[i].fd);
            }
        }
    }

    // ensure Wait knows we are killing this
    uint64_t data = UINT32_MAX;
    write(mShutdownFd, &data, sizeof(data));

    std::scoped_lock<std::mutex> lk(mMtx);
    close(mListenFd);
    close(mShutdownFd);
    mListenFd = -1;
    mShutdownFd = -1;
    for (const int client : mClients) {
        close(client);
    }
    mClients.clear();
}
