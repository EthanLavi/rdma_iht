# Interlocked Hash Table in RDMA

A Rome based IHT implementation.

## Necessary build files to write

- .bazelrc
- WORKSPACE
- BUILD
- spdlog.BUILD
- fmt.BUILD

## Deploying

1. Check availability
2. Create experiment
3. Select Ubuntu 20.04 (OS), r320 (Node Type), name (Experiment Name), hours (Duration)
4. Edit ./rome/scripts/nodefiles/r320.csv with data from listview
5. Wait while configuring
6. cd into ./rome/scripts
7. [ONCE FINISHED] Run sync command to check availability
```{bash}
python rexec.py --nodefile=nodefiles/r320.csv  --remote_user=esl225 --remote_root=/users/esl225/RDMA --local_root=/home/manager/Research/RDMA --sync
```
8. ssh into a node and check your files are present
```{bash}
ssh esl225@apt###.apt.emulab.net
```
9. Run start up script
```{bash}
python rexec.py --nodefile=nodefiles/r320.csv --remote_user=esl225 --remote_root=/users/esl225/RDMA --local_root=/home/manager/Research/RDMA --sync --cmd="cd RDMA/rome/scripts/setup && python3 run.py --resources all"
```
10. Wait while configuring
11. [ONCE FINISHED] Login to nodes or continue to run C&C from cmd line.

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

## Configuring Your Enviornment For Development

- Installations
    - absl (/usr/local/absl) <i>[Abseil Source](https://github.com/abseil/abseil-cpp)</i>
    - rdma_core (?) <i>[RDMA Core Source](https://github.com/linux-rdma/rdma-core)</i>
    - Google Test Framework <i>[gmock Source](https://github.com/google/googletest)</i>
        - gmock (/usr/local/gmock) 
        - gtest (/usr/local/gtest)
    - protos () <i>[Protocol Buffer Source](https://github.com/protocolbuffers/protobuf)</i>

> Note to VSCode users. Edit the include path setting to allow for better Intellisense
   