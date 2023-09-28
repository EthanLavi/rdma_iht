load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@com_google_protobuf//:protobuf.bzl", "py_proto_library")
load("@bazel_skylib//rules:common_settings.bzl", "string_flag")

proto_library(
    name = "experiment_proto",
    srcs = ["protos/experiment.proto"],
    deps = [],
)

cc_proto_library(
    name = "experiment_cc_proto",
    deps = [":experiment_proto"],
)

cc_library(
    name = "ds",
    srcs = ["structures/types.cpp"],
    hdrs = ["structures/iht_ds.h", "structures/hashtable.h", "structures/linked_set.h", "structures/test_map.h", "rome_construction/rdma_shadow.h", "role_server.h", "role_client.h", "common.h", "tcp.h"],
    copts = ["-std=c++2a"],
    deps = [
        ":experiment_cc_proto",
        "@absl//absl/flags:flag",
        "@absl//absl/flags:parse",
        "@absl//absl/status",
        "@absl//absl/status:statusor",
        "@rome//rome/rdma:rdma_memory",
        "@rome//rome/rdma/channel:sync_accessor",
        "@rome//rome/rdma/connection_manager",
        "@rome//rome/rdma/memory_pool",
        "@rome//rome/rdma/memory_pool:remote_ptr",
        "@rome//rome/util:status_util",
        "@rome//rome/util:proto_util",
        "@rome//rome/colosseum:client_adaptor",
        "@rome//rome/colosseum:qps_controller",
        "@rome//rome/colosseum:workload_driver",
        "@rome//rome/colosseum/streams",
    ],
)

cc_binary(
    name = "main",
    srcs = ["main.cc"],
    copts = ["-std=c++2a"],
    deps = [
        ":experiment_cc_proto",
        ":ds"
    ],
)