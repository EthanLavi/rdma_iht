#pragma once

#include <infiniband/verbs.h>
#include <cstdint>
#include <atomic>

#include "rome/rdma/channel/sync_accessor.h"
#include "rome/rdma/connection_manager/connection.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/rdma_memory.h"
#include "rome/logging/logging.h"
#include "common.h"
#include "linked_set.h"

using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

template <typename T>
class RdmaShadow {
    
};