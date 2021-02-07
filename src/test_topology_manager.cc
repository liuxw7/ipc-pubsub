#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include "ips/TopologyManager.h"
#include "ips/Utils.h"

using namespace ips;

// TEST(TopologyManager, SingleClient) {
//    std::string group = GenRandom(8);
//    std::string nodeName1 = GenRandom(8);
//    uint64_t nodeId1 = GenRandom();
//    std::string nodeData1 = GenRandom(8);
//
//    uint64_t joinCount = 0;
//    uint64_t leaveCount = 0;
//    {
//        auto onJoin = [&]([[maybe_unused]] const ips::NodeChange& nodeChange) {
//            joinCount++;
//        };
//        auto onLeave = [&]([[maybe_unused]] const ips::NodeChange& nodeChange) {
//            leaveCount++;
//        };
//
//        TopologyManager mgr1(group, nodeName1, nodeId1, nodeData1, onJoin, onLeave);
//        std::this_thread::sleep_for(std::chrono::milliseconds{100});
//    }
//
//    EXPECT_EQ(joinCount, 1);   // should receive our own
//    EXPECT_EQ(leaveCount, 0);  // not alive to receive our own
//}

TEST(TopologyManager, ClientEntersAndLeaves) {
    std::string group = GenRandom(8);
    std::string nodeName1 = "node1";
    std::string nodeName2 = "node2";
    std::string nodeName3 = "node3";
    uint64_t nodeId1 = GenRandom();
    uint64_t nodeId2 = GenRandom();
    uint64_t nodeId3 = GenRandom();
    std::string nodeData1 = GenRandom(8);
    std::string nodeData2 = GenRandom(8);
    std::string nodeData3 = GenRandom(8);

    uint64_t joinCount = 0;
    uint64_t leaveCount = 0;
    auto onJoin = [&]([[maybe_unused]] const ips::NodeChange& nodeChange) { joinCount++; };
    auto onLeave = [&]([[maybe_unused]] const ips::NodeChange& nodeChange) { leaveCount++; };

    auto mgr1 =
        std::make_shared<TopologyManager>(group, nodeName1, nodeId1, nodeData1, onJoin, onLeave);
    auto mgr2 = std::make_shared<TopologyManager>(group, nodeName2, nodeId2, nodeData2);
    auto mgr3 = std::make_shared<TopologyManager>(group, nodeName3, nodeId3, nodeData3);

    // let everything process, then remove 3
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    mgr3 = nullptr;

    // let everything process, then remove 2
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    mgr2 = nullptr;

    // let mgr3 have time to handle
    std::this_thread::sleep_for(std::chrono::milliseconds{10});

    EXPECT_EQ(joinCount, 3);
    EXPECT_EQ(leaveCount, 2);  // won't recive mgr1's own
}

TEST(TopologyManager, MiddleNodeShouldStillGetAllNodes) {
    std::string group = GenRandom(8);
    std::string nodeName1 = GenRandom(8);
    std::string nodeName2 = GenRandom(8);
    std::string nodeName3 = GenRandom(8);
    uint64_t nodeId1 = GenRandom();
    uint64_t nodeId2 = GenRandom();
    uint64_t nodeId3 = GenRandom();
    std::string nodeData1 = GenRandom(8);
    std::string nodeData2 = GenRandom(8);
    std::string nodeData3 = GenRandom(8);

    uint64_t joinCount = 0;
    uint64_t leaveCount = 0;
    auto onJoin = [&]([[maybe_unused]] const ips::NodeChange& nodeChange) { joinCount++; };
    auto onLeave = [&]([[maybe_unused]] const ips::NodeChange& nodeChange) { leaveCount++; };

    auto mgr1 = std::make_shared<TopologyManager>(group, nodeName1, nodeId1, nodeData1);
    auto mgr2 =
        std::make_shared<TopologyManager>(group, nodeName2, nodeId2, nodeData2, onJoin, onLeave);
    auto mgr3 = std::make_shared<TopologyManager>(group, nodeName3, nodeId3, nodeData3);

    // let everyone process
    std::this_thread::sleep_for(std::chrono::milliseconds{10});

    // kill 1 and 3
    mgr1 = nullptr;
    mgr3 = nullptr;

    // let everyone process
    std::this_thread::sleep_for(std::chrono::milliseconds{10});

    EXPECT_EQ(joinCount, 3);
    EXPECT_EQ(leaveCount, 2);  // won't recive own
}
//
// TEST(TopologyManager, ServerShutdown) {
//    std::string group = GenRandom(8);
//    std::string nodeName1 = GenRandom(8);
//    std::string nodeName2 = GenRandom(8);
//    std::string nodeName3 = GenRandom(8);
//    uint64_t nodeId1 = GenRandom();
//    uint64_t nodeId2 = GenRandom();
//    uint64_t nodeId3 = GenRandom();
//    std::string nodeData1 = GenRandom(8);
//    std::string nodeData2 = GenRandom(8);
//    std::string nodeData3 = GenRandom(8);
//
//    uint64_t joinCount = 0;
//    uint64_t leaveCount = 0;
//    {
//        auto onJoin = [&]([[maybe_unused]] const ips::NodeChange& nodeChange) {
//            joinCount++;
//        };
//        auto onLeave = [&]([[maybe_unused]] const ips::NodeChange& nodeChange) {
//            leaveCount++;
//        };
//
//        TopologyManager mgr1(group, nodeName1, nodeId1, nodeData1);
//        TopologyManager mgr2(group, nodeName2, nodeId2, nodeData2, onJoin, onLeave);
//        TopologyManager mgr3(group, nodeName3, nodeId3, nodeData3);
//        // don't die before we actually send the messages
//        std::this_thread::sleep_for(std::chrono::milliseconds{10});
//
//        // this should trigger 1 to leave (permanently), 2, 3 to leave then rejoin
//        // + 3 leaves, + 2 joins
//        mgr1.Shutdown();
//    }
//
//    EXPECT_EQ(joinCount, 5);
//    EXPECT_EQ(leaveCount, 5);  // won't recive mgr1's own
//}

int main(int argc, char** argv) {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%L%$] [%P %t] [%15!s:%-4#] %v");

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
