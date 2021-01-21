#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>

#include "TopologyStore.h"

class UDSServer;
namespace ipc_pubsub {
class TopologyMessage;
}

class TopologyServer {
   public:
    TopologyServer(std::string_view announcePath);

   private:
    void ApplyUpdate(const ipc_pubsub::TopologyMessage& msg);
    void OnConnect(int fd);
    void OnDisconnect(int fd);
    void OnData(int fd, int64_t len, uint8_t* data);

    std::mutex mMtx;
    std::unordered_map<int, uint64_t> mFdToNode;

    TopologyStore store;
    std::shared_ptr<UDSServer> mServer;
};
