# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_library
cc_library(
    name = "iht_ds",
    srcs = ["iht_ds.cpp"],
    hdrs = ["iht_ds.h", "role_server.h", "role_client.h"],
    copts = ["-std=c++2a"],
    deps = [
        "@rome//rome/rdma:rdma_memory",
        "@rome//rome/rdma/channel:sync_accessor",
        "@rome//rome/rdma/connection_manager",
        "@rome//rome/rdma/memory_pool",
        "@rome//rome/rdma/memory_pool:remote_ptr",
        "@rome//rome/util:status_util",
        "@rome//rome/colosseum:client_adaptor",
        "@rome//rome/colosseum:qps_controller",
        "@rome//rome/colosseum:workload_driver",
        "@rome//rome/colosseum/streams",
    ],
)

# https://docs.bazel.build/versions/master/be/c-cpp.html#cc_binary
cc_binary(
    name = "main",
    srcs = ["main.cc"],
    copts = ["-std=c++2a"],
    deps = [
        ":iht_ds"
    ],
)