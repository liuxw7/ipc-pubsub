load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_test(
    name = "test_topology_manager",
    srcs = ["test_topology_manager.cc"],
    deps = [
        ":TopologyManager",
        ":Utils",
        "@gtest",
    ],
)

cc_binary(
    name = "topology_node",
    srcs = ["topology_node.cc"],
    deps = [
        ":TopologyManager",
        ":Utils",
        "@com_github_gabime_spdlog//:spdlog",
    ],
)

cc_binary(
    name = "unix_dgram_reader",
    srcs = ["unix_dgram_reader.cc"],
)

cc_binary(
    name = "unix_dgram_writer",
    srcs = ["unix_dgram_writer.cc"],
)

#cc_library(
#    name = "TopologyStore",
#    srcs = ["TopologyStore.cc"],
#    hdrs = ["TopologyStore.h"],
#    deps = [
#        "//protos:index_cc_proto",
#        "@com_github_gabime_spdlog//:spdlog",
#    ],
#)

cc_library(
    name = "TopologyServer",
    srcs = ["TopologyServer.cc"],
    hdrs = ["TopologyServer.h"],
    deps = [
        ":UDSServer",
        ":Utils",
        "//protos:index_cc_proto",
        "@com_github_gabime_spdlog//:spdlog",
    ],
)

cc_library(
    name = "TopologyManager",
    srcs = ["TopologyManager.cc"],
    hdrs = ["TopologyManager.h"],
    deps = [
        ":TopologyServer",
        ":UDSClient",
        ":Utils",
        "//protos:index_cc_proto",
        "@com_github_gabime_spdlog//:spdlog",
    ],
)

cc_library(
    name = "UDSClient",
    srcs = ["UDSClient.cc"],
    hdrs = ["UDSClient.h"],
    deps = [
        ":Utils",
        "@com_github_gabime_spdlog//:spdlog",
    ],
)

cc_library(
    name = "UDSServer",
    srcs = ["UDSServer.cc"],
    hdrs = ["UDSServer.h"],
    deps = [
        ":Utils",
        "@com_github_gabime_spdlog//:spdlog",
    ],
)

cc_library(
    name = "Utils",
    srcs = ["Utils.cc"],
    hdrs = ["Utils.h"],
)
