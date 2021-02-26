load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_binary(
    name = "ponger",
    srcs = ["ponger.cc"],
    deps = [
        "//lib:IPCNode",
        "@com_github_gabime_spdlog//:spdlog",
    ],
)

cc_binary(
    name = "pinger",
    srcs = ["pinger.cc"],
    deps = [
        "//lib:IPCNode",
        "@com_github_gabime_spdlog//:spdlog",
    ],
)

cc_binary(
    name = "topology_node",
    srcs = ["topology_node.cc"],
    deps = [
        "//lib:TopologyManager",
        "//lib:Utils",
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

cc_binary(
    name = "standalone_publisher",
    srcs = ["standalone_publisher.cc"],
    deps = ["//lib:Publisher"],
)

cc_binary(
    name = "standalone_subscriber",
    srcs = ["standalone_subscriber.cc"],
    deps = [
        "//lib:IPCMessage",
        "//lib:Subscriber",
    ],
)
