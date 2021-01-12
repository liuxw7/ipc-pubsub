package(default_visibility = ["//visibility:public"])

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
)
