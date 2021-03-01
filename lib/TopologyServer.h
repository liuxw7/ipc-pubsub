#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "protos/index.pb.h"

namespace ips {
class UDSServer;

class TopologyServer {
   public:
    TopologyServer(std::string_view announcePath,
                   const std::vector<ips::TopologyMessage>& digest = {});
    ~TopologyServer();
    void Shutdown();

   private:
    struct Client {
        // for each file descriptor, the maximum sequence sent to the node
        uint64_t seq = 0;
        uint64_t nodeId = 0;
        std::vector<ips::TopologyMessage> history;
    };

    // These functions lock, everything else should not
    void OnConnect(int fd);
    void OnDisconnect(int fd);
    void OnData(int fd, int64_t len, uint8_t* data);

    // These functions expect lock to be held already
    void Broadcast();
    void PruneRemoved();
    void AddOldMessage(const TopologyMessage& msg, Client* client);
    void AddNovelMessage(const TopologyMessage& msg, Client* client);
    static bool IsRedundant(const TopologyMessage& msg, const Client& client);

    // after a period has passed without active nodes connecting, send LEAVE messages
    void PurgeDisconnected();

    std::mutex mMtx;
    std::unordered_map<int, Client> mClients;

    uint64_t mNextSeq = 1;  // start at 1 because protobuf default uint64 is 0
    std::vector<ips::TopologyMessage> mHistory;
    std::shared_ptr<UDSServer> mServer;

    // so we can quickly determine if a message is redundant or not
    std::unordered_set<uint64_t> mMessageHashes;

    // calls PurgeDisconnected() one time a fixed time after startup
    std::thread mPurgeThread;
};
}  // namespace ips
