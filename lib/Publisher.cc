#include "ipc_pubsub/Publisher.h"

#include <fcntl.h>  // For O_* constants
#include <string.h>
#include <sys/mman.h>  // for shm_open
#include <sys/socket.h>
#include <sys/stat.h>  // For mode constants
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cassert>
#include <functional>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "ipc_pubsub/Utils.h"

namespace ipc_pubsub {
Publisher::Publisher() {
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
    if (bind(mFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        perror("bind error");
        exit(-1);
    }

    if (listen(mFd, 5) == -1) {
        perror("listen error");
        exit(-1);
    }

    mAcceptThread = std::thread([this]() {
        while (1) {
            int cl;
            if ((cl = accept(mFd, nullptr, nullptr)) == -1) {
                perror("accept error");
            } else {
                std::cerr << "Accepted" << std::endl;
                std::lock_guard<std::mutex> lk(mMtx);
                mClients.push_back(cl);
            }
        }
    });
}

bool Publisher::Send(const std::string& meta, size_t len, const uint8_t* data) {
    const std::string name = GenRandom(8);

    // open and write data
    int shmFdWrite = shm_open(name.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (shmFdWrite == -1) {
        perror("Failed to create shm");
        return false;
    }

    // unlink when we leave this function, since it will have been shared by
    // then or failed
    OnRet onRet1([&name, shmFdWrite]() {
        shm_unlink(name.c_str());
        close(shmFdWrite);
    });

    if (ssize_t written = write(shmFdWrite, data, len); written != ssize_t(len)) {
        perror("Failed to write to shm");
        return false;
    }

    // send read only version to other processes, close after we finish sending
    int shmFdSend = shm_open(name.c_str(), O_RDONLY, S_IRUSR);
    OnRet onRet2([shmFdSend]() { close(shmFdSend); });

    // Construct header
    struct msghdr msgh;

    std::vector<char> metadata;
    std::copy(meta.begin(), meta.end(), std::back_inserter(metadata));
    metadata.push_back(0);

    // Vector of data to send, NOTE: we must always send at least one byte
    // This is the data that will be sent in the actual socket stream
    struct iovec iov;
    iov.iov_base = metadata.data();
    iov.iov_len = metadata.size();

    // Don't need destination because we are using a connection
    msgh.msg_name = nullptr;
    msgh.msg_namelen = 0;
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;  // sending one message

    // Allocate a char array of suitable size to hold the ancillary data.
    // However, since this buffer is in reality a 'struct cmsghdr', use a
    // union to ensure that it is aligned as required for that structure.
    union {
        char buf[CMSG_SPACE(sizeof(shmFdSend))];
        /* Space large enough to hold an 'int' */
        struct cmsghdr align;
    } controlMsg;
    msgh.msg_control = controlMsg.buf;
    msgh.msg_controllen = sizeof(controlMsg.buf);

    // The control message buffer must be zero-initialized in order
    // for the CMSG_NXTHDR() macro to work correctly.
    memset(controlMsg.buf, 0, sizeof(controlMsg.buf));

    struct cmsghdr* cmsgp = CMSG_FIRSTHDR(&msgh);
    cmsgp->cmsg_len = CMSG_LEN(sizeof(shmFdSend));
    cmsgp->cmsg_level = SOL_SOCKET;
    cmsgp->cmsg_type = SCM_RIGHTS;
    memcpy(CMSG_DATA(cmsgp), &shmFdSend, sizeof(shmFdSend));

    std::cerr << reinterpret_cast<char*>(msgh.msg_iov->iov_base) << std::endl;
    std::cerr << strlen(reinterpret_cast<char*>(msgh.msg_iov->iov_base)) << std::endl;
    std::cerr << msgh.msg_iov[0].iov_len << std::endl;
    // Send
    for (const int client : mClients) {
        ssize_t ns = sendmsg(client, &msgh, 0);
        if (ns == -1) {
            std::cerr << "Failed to send to " << client << std::endl;
        }
    }
    return true;
}
}  // namespace ipc_pubsub
