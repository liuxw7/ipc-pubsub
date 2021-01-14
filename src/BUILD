cc_test(
    name = "test_dispatcher",
    srcs = ["test_dispatcher.cc"],
    deps = [
        ":Dispatcher",
        ":ShmMessage",
    ],
)

cc_library(
    name = "Utils",
    srcs = ["Utils.cc"],
    hdrs = ["Utils.h"],
)

cc_library(
    name = "ShmMessage",
    srcs = ["ShmMessage.cc"],
    hdrs = ["ShmMessage.h"],
    linkopts = ["-lrt"],
    deps = [":Utils"],
)

cc_library(
    name = "Dispatcher",
    srcs = ["Dispatcher.cc"],
    hdrs = ["Dispatcher.h"],
    deps = [
        ":ShmMessage",
        ":Utils",
    ],
)

cc_library(
    name = "IPCMessenger",
    srcs = ["IPCMessenger.cc"],
    hdrs = ["IPCMessenger.h"],
    linkopts = [
        "-lrt",
    ],
    deps = [
        ":Dispatcher",
        "//protos:index_cc_proto",
    ],
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
