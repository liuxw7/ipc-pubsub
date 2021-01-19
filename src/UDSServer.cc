#include "UDSServer.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cassert>
#include <mutex>

#include "Utils.h"

std::shared_ptr<UDSServer> UDSServer::Create(std::string_view sockPath) {
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

    return std::make_shared<UDSServer>(sockFd);
}

UDSServer::UDSServer(int fd) : mListenFd(fd) {}

void UDSServer::Broadcast(size_t len, uint8_t* data) {
    std::unordered_set<int> clients;
    {
        std::lock_guard<std::mutex> lk(mMtx);
        clients = mClients;
    }
    for (int client : clients) write(client, data, len);
}

int UDSServer::LoopUntilShutdown(int shutdownEventFd, std::function<void(int)> onConnect,
                                 std::function<void(int)> onDisconnect,
                                 std::function<void(int, size_t, uint8_t*)> onData) {
    // std::unordered_map<int, uint64_t> fdToNodeId;
    // std::unordered_map<uint64_t, int> nodeIdToFd;

    std::vector<pollfd> pollFds(2);
    pollFds[0].fd = shutdownEventFd;
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
                pollFds.push_back({
                    .fd = fd,
                    .events = POLLIN | POLLHUP | POLLRDHUP,
                });
            }
        }

        // read client file descriptors
        if (int ret = poll(pollFds.data(), pollFds.size(), 0); ret < 0) {
            perror("Failed to poll");
            return -1;
        }

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
                onConnect(newClientFd);
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
                onData(pollFds[i].fd, nBytes, buffer);
            } else if ((pollFds[i].revents & (POLLHUP | POLLRDHUP)) != 0) {
                onDisconnect(pollFds[i].fd);
                std::lock_guard<std::mutex> lk(mMtx);
                mClients.erase(pollFds[i].fd);
            }
        }
    }
}
