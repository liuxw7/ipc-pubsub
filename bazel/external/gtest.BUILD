package(default_visibility = ["//visibility:public"])

# Library that defines the FRIEND_TEST macro.
cc_library(
    name = "gtest_prod",
    hdrs = ["googletest/include/gtest/gtest_prod.h"],
    includes = ["googletest/include"],
)

# Google Test including Google Mock
cc_library(
    name = "gtest",
    srcs = glob(
        include = [
            "googletest/src/*.cc",
            "googletest/src/*.h",
            "googletest/include/gtest/**/*.h",
            "googlemock/src/*.cc",
            "googlemock/include/gmock/**/*.h",
        ],
        exclude = [
            "googletest/src/gtest-all.cc",
            "googletest/src/gtest_main.cc",
            "googlemock/src/gmock-all.cc",
            "googlemock/src/gmock_main.cc",
        ],
    ),
    hdrs = glob([
        "googletest/include/gtest/*.h",
        "googlemock/include/gmock/*.h",
    ]),
    copts = [
        "-pthread",
        "-Wno-undef",
        "-Wno-unused-member-function",
        "-Wno-zero-as-null-pointer-constant",
        "-Wno-used-but-marked-unused",
        "-Wno-missing-noreturn",
        "-Wno-covered-switch-default",
        "-Wno-disabled-macro-expansion",
        "-Wno-weak-vtables",
        "-Wno-switch-enum",
        "-Wno-missing-prototypes",
        "-Wno-deprecated-copy-dtor",
    ],
    includes = [
        "googlemock",
        "googlemock/include",
        "googletest",
        "googletest/include",
    ],
    linkopts = ["-pthread"],
)

cc_library(
    name = "gtest_main",
    srcs = ["googlemock/src/gmock_main.cc"],
    deps = [":gtest"],
)
