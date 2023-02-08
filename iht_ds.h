#include <infiniband/verbs.h>
#include <cstdint>

#include "rome/rdma/channel/sync_accessor.h"
#include "rome/rdma/connection_manager/connection.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/rdma_memory.h"

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

class RdmaIHT {
public:
    using conn_type = MemoryPool::conn_type;

    RdmaIHT(MemoryPool::Peer self, std::unique_ptr<MemoryPool::cm_type> cm);

    void Init(MemoryPool::Peer host, const std::vector<MemoryPool::Peer> &peers);

    bool contains();
    bool insert();
    bool remove();
private:
    bool is_host_;
    MemoryPool::Peer self_;
    MemoryPool pool_;
};
