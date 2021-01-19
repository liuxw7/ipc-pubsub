#pragma once
#include <memory>
#include <string>
#include <string_view>
class IPCNode {
   public:
    static std::shared_ptr<IPCNode> Create(std::string_view fname);

    IPCNode(std::string_view fname, int shutdownFd, int noLeaderFd);

    ~IPCNode();

   private:
    std::string mSocketName;
    int mSock;
};
