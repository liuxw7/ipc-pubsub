cc_library(
    name = "IPCMessenger",
    srcs = ["IPCMessenger.cc"],
    hdrs = ["IPCMessenger.h"],
    linkopts = [
        "-lrt",
    ],
    deps = ["//protos:index_cc_proto"],
)

cc_binary(
    name = "pinger",
    srcs = ["pinger.cc"],
    deps = [":IPCMessenger"],
)

cc_binary(
    name = "ponger",
    srcs = ["ponger.cc"],
    deps = [":IPCMessenger"],
)
