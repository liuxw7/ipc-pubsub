workspace(name = "com_github_micahcc_ipc_pubsub")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

# rules_cc defines rules for generating C++ code from Protocol Buffers.
http_archive(
    name = "com_google_protobuf",
    sha256 = "37093b5bafc09ceaef8ba12f98701ddd0ba372995da5154782c6301eb424418a",
    strip_prefix = "protobuf-f5a3b92cf7b379f2b8c33e8b30ca029a368be512",
    urls = [
        "https://github.com/protocolbuffers/protobuf/archive/f5a3b92cf7b379f2b8c33e8b30ca029a368be512.tar.gz",
    ],
)

load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")

protobuf_deps()

SPDLOG_VERSION = "1.7.0"

http_archive(
    name = "com_github_gabime_spdlog",
    build_file = "@com_github_micahcc_ipc_pubsub//bazel/external:spdlog.BUILD",
    sha256 = "f0114a4d3c88be9e696762f37a7c379619443ce9d668546c61b21d41affe5b62",
    strip_prefix = "spdlog-%s" % SPDLOG_VERSION,
    urls = ["https://github.com/gabime/spdlog/archive/v%s.tar.gz" % SPDLOG_VERSION],
)

FMTLIB_VERSION = "7.1.3"

http_archive(
    name = "com_github_fmtlib_fmt",
    build_file = "@com_github_micahcc_ipc_pubsub//bazel/external:fmtlib.BUILD",
    sha256 = "5d98c504d0205f912e22449ecdea776b78ce0bb096927334f80781e720084c9f",
    strip_prefix = "fmt-%s" % FMTLIB_VERSION,
    urls = ["https://github.com/fmtlib/fmt/releases/download/%s/fmt-%s.zip" % (FMTLIB_VERSION, FMTLIB_VERSION)],
)

http_archive(
    name = "gtest",
    build_file = "@com_github_micahcc_ipc_pubsub//bazel/external:gtest.BUILD",
    sha256 = "9dc9157a9a1551ec7a7e43daea9a694a0bb5fb8bec81235d8a1e6ef64c716dcb",
    strip_prefix = "googletest-release-1.10.0",
    urls = [
        "https://github.com/google/googletest/archive/release-1.10.0.tar.gz",
    ],
)
