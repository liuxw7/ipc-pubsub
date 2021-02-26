#include "ipc_pubsub/IPCMessage.h"

#include <spdlog/spdlog.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace ipc_pubsub {

IPCMessage::~IPCMessage() {
    munmap(mBlobPtr, mBlobSize);
    if (mFd != -1) close(mFd);
}

std::shared_ptr<IPCMessage> IPCMessage::Create(size_t vecLen, const struct msghdr& msgh) {
    int recvFd;
    struct cmsghdr* cmsgp = CMSG_FIRSTHDR(&msgh);
    if (cmsgp == nullptr || cmsgp->cmsg_len != CMSG_LEN(sizeof(recvFd))) {
        SPDLOG_ERROR("bad cmsg header / message length");
        return nullptr;
    }
    if (cmsgp->cmsg_level != SOL_SOCKET) {
        SPDLOG_ERROR("cmsg_level != SOL_SOCKET");
        return nullptr;
    }
    if (cmsgp->cmsg_type != SCM_RIGHTS) {
        SPDLOG_ERROR("cmsg_type != SCM_RIGHTS");
        return nullptr;
    }

    if (msgh.msg_iovlen == 0) {
        SPDLOG_ERROR("No vector data!?");
        return nullptr;
    }
    if (msgh.msg_iovlen != 1) {
        SPDLOG_ERROR("Expected exactly 1 pieces of vector data, others will be ignoed");
        return nullptr;
    }

    // construct metadata from first vector payload
    std::string_view meta(reinterpret_cast<const char*>(msgh.msg_iov[0].iov_base), vecLen);

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

    return std::make_shared<IPCMessage>(meta, reinterpret_cast<uint8_t*>(mappedData), blobLen,
                                        recvFd);
}

}  // namespace ipc_pubsub
