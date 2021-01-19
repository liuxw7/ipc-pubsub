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
