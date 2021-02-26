load("@rules_cc//cc:defs.bzl", "cc_library")

cc_test(
    name = "test_topology_manager",
    srcs = ["test_topology_manager.cc"],
    deps = [
        ":TopologyManager",
        ":Utils",
        "@gtest",
    ],
)

cc_library(
    name = "IPCMessage",
    srcs = ["IPCMessage.cc"],
    hdrs = ["IPCMessage.h"],
    include_prefix = "ipc_pubsub",
    includes = ["."],
    linkopts = [
        "-pthread",
        "-lpthread",
        "-lrt",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@com_github_gabime_spdlog//:spdlog",
    ],
)

cc_library(
    name = "Subscriber",
    srcs = ["Subscriber.cc"],
    hdrs = ["Subscriber.h"],
    include_prefix = "ipc_pubsub",
    includes = ["."],
    linkopts = [
        "-pthread",
        "-lpthread",
        "-lrt",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":IPCMessage",
    ],
)

cc_library(
    name = "Publisher",
    srcs = ["Publisher.cc"],
    hdrs = ["Publisher.h"],
    include_prefix = "ipc_pubsub",
    includes = ["."],
    linkopts = [
        "-pthread",
        "-lpthread",
        "-lrt",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":Utils",
    ],
)

cc_library(
    name = "TopologyServer",
    srcs = ["TopologyServer.cc"],
    hdrs = ["TopologyServer.h"],
    include_prefix = "ipc_pubsub",
    includes = ["."],
    deps = [
        ":UDSServer",
        ":Utils",
        "//protos:index_cc_proto",
        "@com_github_gabime_spdlog//:spdlog",
    ],
)

cc_library(
    name = "IPCNode",
    srcs = ["IPCNode.cc"],
    hdrs = ["IPCNode.h"],
    include_prefix = "ipc_pubsub",
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [":TopologyManager"],
)

cc_library(
    name = "TopologyManager",
    srcs = ["TopologyManager.cc"],
    hdrs = ["TopologyManager.h"],
    include_prefix = "ipc_pubsub",
    includes = ["."],
    visibility = ["//visibility:public"],
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
    include_prefix = "ipc_pubsub",
    includes = ["."],
    deps = [
        ":Utils",
        "@com_github_gabime_spdlog//:spdlog",
    ],
)

cc_library(
    name = "UDSServer",
    srcs = ["UDSServer.cc"],
    hdrs = ["UDSServer.h"],
    include_prefix = "ipc_pubsub",
    includes = ["."],
    deps = [
        ":Utils",
        "@com_github_gabime_spdlog//:spdlog",
    ],
)

cc_library(
    name = "Utils",
    srcs = ["Utils.cc"],
    hdrs = ["Utils.h"],
    include_prefix = "ipc_pubsub",
    includes = ["."],
    visibility = ["//visibility:public"],
)
