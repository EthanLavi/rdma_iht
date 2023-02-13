# Interlocked Hash Table in RDMA

A Rome based IHT implementation.

## Fixing changes

Made change to cc_toolchain_config.bzl
> By changing tool_path for gcc on line 74 from /usr/bin/clang to /usr/bin/clang-12, I was able to compile correctly

## Other necessary files

- .bazelrc
- WORKSPACE
- BUILD
- spdlog.BUILD
- fmt.BUILD

## Errors

> [2023-02-12 21:1142 thread:54545] [error] [external/rome/rome/rdma/memory_pool/memory_pool_impl.h:148] INTERNAL: ibv_modify_qp(): Invalid argument
