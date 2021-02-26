#pragma once

#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ipc_pubsub {
class Publisher {
   public:
    Publisher();
    bool Send(const std::string& meta, size_t len, const uint8_t* data);

    int mFd;
    std::mutex mMtx;
    std::vector<int> mClients;
    std::thread mAcceptThread;
};
}  // namespace ipc_pubsub
