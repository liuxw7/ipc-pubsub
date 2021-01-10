load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_proto//proto:defs.bzl", "proto_library")

cc_library(
    name = "IPCMessenger",
    srcs = ["IPCMessenger.cc"],
    hdrs = ["IPCMessenger.h"],
)

#
#cc_library(
#    name = "IPCPubSub",
#    srcs = ["IPCPubSub.cc"],
#    hdrs = ["IPCPubSub.h"],
#)
#
#cc_binary(
#    name = "pinger",
#    srcs = ["pinger.cc"],
#    deps = [":IPCPubSub"],
#)
#
#cc_binary(
#    name = "ponger",
#    srcs = ["ponger.cc"],
#    deps = [":IPCPubSub"],
#)

# Convention:
# A cc_proto_library that wraps a proto_library named foo_proto
# should be called foo_cc_proto.
cc_proto_library(
    name = "index_cc_proto",
    deps = [":index_proto"],
)

proto_library(
    name = "index_proto",
    srcs = ["index.proto"],
    deps = [
    ],
)
