load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_binary(
    name = "topology_node",
    srcs = ["topology_node.cc"],
    deps = [":TopologyManager"],
)

cc_binary(
    name = "unix_dgram_reader",
    srcs = ["unix_dgram_reader.cc"],
)

cc_binary(
    name = "unix_dgram_writer",
    srcs = ["unix_dgram_writer.cc"],
)

cc_library(
    name = "TopologyManager",
    srcs = ["TopologyManager.cc"],
    hdrs = ["TopologyManager.h"],
    deps = [
        ":UDSClient",
        ":UDSServer",
        ":Utils",
        "//protos:index_cc_proto",
    ],
)

cc_library(
    name = "UDSClient",
    srcs = ["UDSClient.cc"],
    hdrs = ["UDSClient.h"],
    deps = [":Utils"],
)

cc_library(
    name = "UDSServer",
    srcs = ["UDSServer.cc"],
    hdrs = ["UDSServer.h"],
    deps = [":Utils"],
)

cc_library(
    name = "Utils",
    srcs = ["Utils.cc"],
    hdrs = ["Utils.h"],
)

#cc_library(
#    name = "IPCNode",
#    srcs = ["IPCNode.cc"],
#    hdrs = [
#        "IPCNode.h",
#    ],
#    deps = [
#        ":UDSClient",
#        ":UDSServer",
#        "//protos:index_cc_proto",
#    ],
#)

#
#cc_binary(
#    name = "ipc_node_test",
#    srcs = ["ipc_node_test.cc"],
#    deps = [":IPCNode"],
#)
#
cc_library(
    name = "lib",
    srcs = [
        "Dispatcher.cc",
        "Globals.cc",
        "IPCMessenger.cc",
        "ShmMessage.cc",
        "Utils.cc",
    ],
    hdrs = [
        "Dispatcher.h",
        "Globals.h",
        "IPCMessenger.h",
        "ShmMessage.h",
        "Utils.h",
    ],
    linkopts = ["-lrt"],
    deps = ["//protos:index_cc_proto"],
)

cc_binary(
    name = "pinger",
    srcs = ["pinger.cc"],
    deps = [
        ":lib",
    ],
)

cc_binary(
    name = "ponger",
    srcs = ["ponger.cc"],
    deps = [
        ":lib",
    ],
)

cc_test(
    name = "test_dispatcher",
    srcs = ["test_dispatcher.cc"],
    deps = [
        ":lib",
    ],
)
