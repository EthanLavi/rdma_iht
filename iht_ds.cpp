#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <infiniband/verbs.h>
#include <cstdio>
#include <iostream>

#include "rome/rdma/memory_pool/remote_ptr.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/logging/logging.h"
#include "iht_ds.h"

using rome::rdma::ConnectionManager;
using rome::rdma::MemoryPool;
using rome::rdma::remote_nullptr;
using rome::rdma::remote_ptr;
using rome::rdma::RemoteObjectProto;

template class RdmaIHT<int, int, 8, 128>;  