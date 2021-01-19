
constexpr size_t MAX_NOTIFY_SIZE = 2048;
// Managers a set of unix domain socket servers and clients.

// Leader Thread
// construct socket
// while true
//    select shutdown pipe or accept
//    if shutdown
//      for each server thread
//          send byte to shutdown pipe
//      quit
//    if accept()
//       create shutdown pipe pipe
//       create server thread (that calls accept() )
//
struct OnRet {
    OnRet(std::function<void()> f) : mF(f){};
    std::function<void()> mF;
};

// Read from input socket
ServerEchoReadLoop(int shutdownEventFd, ) {}

/*
 *
 */
class TopologyManager {
    void Apply(const TopologyMessage& msg);

   private:
    std::vector<Nodes> mNodes;
};

/*
 * Every node starts as a leader thread, but won't start accepting connections until
 * the current leader shuts down. Broadcasts topology
 */
void LeaderLoop(std::string_view sockPath, int shutdownEventFd, int noLeaderFd) {
    struct sockaddr_un addr;
    char buf[MAX_NOTIFY_SIZE];
    assert(!sockPath.empty());
    assert(sockPath.size() + 1 < sizeof(addr.sun_path));
    assert(sockPath[0] != '\0');

    OnRet onRet1([shutdownEventFd]() {
        // when we exit make sure everyone else gets the memo
        uint64_t tmp = 1;
        int nBytes = write(shutdownEventFd, &tmp, sizeof(tmp));
    });

    int sockFd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (sockFd == -1) {
        perror("socket error");
        return;
    }

    // wait until we become the leader (can take ownership of the socketPath)
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::copy(sockPath.begin(), sockPath.end(), addr.sun_path);
    addr.sun_path[sockPath.size()] = 0;
    while (true) {
        if (bind(sockFd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            // success, break
            break;
        }

        struct pollfd fds[2];
        fds[0].fd = shutdownEventFd;
        fds[0].events = POLLIN;
        fds[0].revents = 0;

        fds[1].fd = noLeaderFd;
        fds[1].events = POLLIN;
        fds[1].revents = 0;

        int ret = poll(&fds, 2, nullptr);
        if (ret < 0) {
            perror("Failed to poll");
            return;
        }

        if (fds[0].revents != 0) {
            // shutdown event received, exit
            return;
        }

        if (fds[1].revents != 0) {
            // leader shutdown which means we need a new leader try to bind again
            continue;
        }
    }

    // once we have reached this point we are the leader, accept connections and
    // relay messages back out,
    // NOTE: once we become leader you might think we need a warm topology,but
    // all the known clients should immediately try to connect to us, so we'll
    // rebuild the topology from that
    if (listen(sockFd, 10) == -1) {
        perror("listen error");
        return;
    }

    TopologyManager topologyManager;
    std::unordered_map<int, uint64_t> fdToNodeId;

    std::vector<pollfd> fds(2);
    fds[0].fd = shutdownEventFd;
    fds[0].events = POLLIN;

    fds[1].fd = sockFd;
    fds[1].events = POLLIN;

    while (true) {
        // read client file descriptors
        fds.resize(2);
        for (auto pair : fdToNodeId) {
            fds.push_back({
                .fd = pair.first,
                .events = POLLIN,
                .revents = 0,
            });
        }

        if (int ret = poll(fds.data(), fds.size(), nullptr); ret < 0) {
            perror("Failed to poll");
            return;
        }

        if (fds[0].revents != 0) {
            // shutdown event received, exit
            return;
        }

        if (fds[1].revents != 0) {
            // new client knocking
            int newClientFd = accept(sockFd, NULL, NULL);
            if (newClientFd == -1) {
                perror("accept error");
                return;
            } else {
                fdToNodeId[newClientFd] = 0;
                auto completeMsg = topology->GetCompleteTopology();
                completeMsg.SerializeToFileDescriptor(newClientFd);
            }
        }

        for (size_t i = 2; i < fds.size(); ++i) {
            if (fds[i].revents == 0) {
                // no event for the filedescriptor
                continue;
            }

            NodeUpdateMessage msg;
            if (fds[i].revents & POLLIN != 0) {
                // new data ready
                bool success = msg.ParseFromFileDescriptor(fds[i].fd);
                if (success) {
                    topology->Apply(msg);
                    fdToNodeId[fds[i].fd] = msg.node_id();
                } else {
                    std::cerr << "Failed to parse" << std::endl;
                }

            } else if (fds[i].revents & (POLLHUP | POLLRDHUP) != 0) {
                // closed, create delete event for it
                const uint64_t nodeId = fdToNodeId[fds[i].fd];
                if (nodeId == 0) {
                    std::cerr << "Client disconnected without ever sending metadata" << std::endl;
                } else {
                    msg.set_node_id(nodeId);
                    msg.set_delete(true);
                    topology->Apply(msg);
                }
                fdToNodeId.erase(fds[i].fd);
            }

            // broadcast update message to all clients
            for (auto pair : fdToNodeId) {
                // don't really care if the send fails, because that just means
                // we'll get a node dying notification later
                msg.SerializeToFileDescriptor(pair.first);
            }
        }
    }
}

/*
 * Attaches to the leader and records topolyg messages into topologyManager
 */
void ClientLoop(std::string_view sockPath, int shutdownEventFd, int noLeaderEventFd,
                TopologyManager* topologyManager) {
    assert(!sockPath.empty());
    assert(sockPath.size() + 1 < sizeof(addr.sun_path));
    assert(sockPath[0] != '\0');

    OnRet onRet1([shutdownEventFd]() {
        // when we exit make sure everyone else gets the memo
        uint64_t tmp = 1;
        int nBytes = write(shutdownEventFd, &tmp, sizeof(tmp));
    });

    int readFd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (readFd == -1) {
        perror("socket error");
        return;
    }

    OnRet onRet2([readFd]() { close(readFd); });

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::copy(sockPath.begin(), sockPath.end(), addr.sun_path);
    addr.sun_path[sockPath.size()] = 0;

    struct pollfd fds[2];
    fds[0].fd = sockFd;
    fds[0].events = POLLIN | POLLRDHUP | POLLHUP;
    fds[0].revents = 0;

    fds[1].fd = shutdownEventFd;
    fds[1].events = POLLIN;
    fds[1].revents = 0;
    while (true) {
        // connect
        if (connect(readFd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            perror("connect error");
            // send noLeaderEventFd signal, this triggers the leader thread to construct a
            // leader socket, sleep a short time so that the leader has time to
            // construct the socket again
            uint64_t tmp = 1;
            int nBytes = write(noLeaderEventFd, &tmp, sizeof(tmp));
            sleep(1);
            continue;
        }

        // now that we are connected field events from leader OR shutdown event
        // wait for it to close or shutdown event
        while (1) {
            int ret = poll(&fds, 2, nullptr);
            if (ret < 0) {
                perror("Failed to poll");
                return;
            }

            if (fds[1].revents != 0) {
                // shutdown event received, exit
                return;
            }

            if (fds[0].revents & (POLLHUP | POLLRDHUP)) {
                // leader shutdown which means we need a new leader, signal then break the
                // current read loop so we can reconnect
                uint64_t tmp = 1;
                int nBytes = write(noLeaderEventFd, &tmp, sizeof(tmp));
                return;
            } else if (fds[0].revents & POLLIN) {
                // socket has data, read it
                bool ret = topologyMessage.ParseFromFileDescriptor(readFd);
                if (ret) {
                    topologyManager->Apply(topologyMessage);
                } else {
                    perror("Failed to read");
                }
            }
        }
    }
}

//
// Publish
// create shared memory
// unlink it
// for each reader on topic:
//    send file descriptor to each client
//
// shutdown
// connect to leader, send empty message then disconnect (spuriously unblocking leader and
// server threads)
//

//
class UDSMeshNode {
   public:
    std::shared_ptr<UDSMeshNode> Create(std::string_view fname) {
        int noLeaderFd = eventfd(0, EFD_SEMAPHORE);
        if (noLeaderFd < 0) {
            perror("Failed to create no-leader event fd");
            return nullptr;
        }

        int shutdownFd = eventfd(0, EFD_SEMAPHORE);
        if (noLeaderFd < 0) {
            perror("Failed to create shutdown event fd");
            return nullptr;
        }

        return std::make_shared<UDSMeshNode>(fname, noLeaderFd, shutdownFd);
    }

    UDSMeshNode(std::string_view fname, int shutdownFd, int noLeaderFd)
        : mSocketName(fname),
          mShutdownFd(shutdownFd),
          mNoLeaderFd(noLeaderFd),
          mTopologyManager(new TopologyManager) {
        mLeaderThread = std::thread([this]() { LeaderLoop(mShutdownFd, mNoLeaderFd); });
        mClientThread = std::thread(
            [this]() { ClientLoop(mSocketName, mShutdownFd, mNoLeaderFd, mTopologyManager); });
    }

    ~UDSMeshNode() {
        // notify all threads it is time to shut down
        uint64_t tmp = 1;
        write(mShutdownFd, &tmp, sizeof(tmp));

        mLeaderThread.join();
        mClientThread.join();
        // close(mSock);

        // TODO(micah) unlink if we are the leader and there are no clients?
        // unlink(NAME.c_str());
    }

    std::string mSocketName;
    int mSock;
};
