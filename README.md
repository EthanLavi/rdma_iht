# Interlocked Hash Table in RDMA

A Rome based IHT implementation.

## Necessary build files to write

- .bazelrc
- WORKSPACE
- BUILD
- spdlog.BUILD
- fmt.BUILD

## Errors

### Issue connecting to peers

```
[2023-02-12 21:1142 thread:54545] [error] [external/rome/rome/rdma/memory_pool/memory_pool_impl.h:148] INTERNAL: ibv_modify_qp(): Invalid argument
```

Solved by making sure peers didn't include self 
<br><br>

### Barrier Dependency Issue

```
fatal error: 'barrier' file not found
```
By changing tool_path for gcc on line 74 from /usr/bin/clang to /usr/bin/clang-12, I was able to compile correctly 
<br><br>

### Other issues

None