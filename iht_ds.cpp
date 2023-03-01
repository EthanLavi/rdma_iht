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

RdmaIHT::RdmaIHT(MemoryPool::Peer self, 
                std::unique_ptr<MemoryPool::cm_type> cm, struct config confs) 
                : self_(self), pool_(self, std::move(cm)), elist_size(confs.elist_size), plist_size(confs.plist_size) {}


void RdmaIHT::Init(MemoryPool::Peer host, const std::vector<MemoryPool::Peer> &peers) {

    is_host_ = self_.id == host.id;
    uint32_t block_size = 1 << 20;

    auto status = pool_.Init(block_size, peers);
    // ROME_CHECK_OK(ROME_RETURN(status), status);

    if (is_host_){
        // Host machine, it is my responsibility to initiate configuration

        // Allocate data in pool
		RemoteObjectProto proto;
        remote_plist iht_root = pool_.Allocate<PList>();
        InitPList(iht_root);
        this->root = iht_root;
        proto.set_raddr(iht_root.address());

        // Iterate through peers
        for (auto p = peers.begin(); p != peers.end(); p++){
            // Form a connection with the machine
            auto conn_or = pool_.connection_manager()->GetConnection(p->id);
            // ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);

            // Send the proto over
            status = conn_or.value()->channel()->Send(proto);
            // ROME_CHECK_OK(ROME_RETURN(status), status);
        }
    } else {
        // Listen for a connection
        auto conn_or = pool_.connection_manager()->GetConnection(host.id);
        // ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);

        // Try to get the data from the machine, repeatedly trying until successful
        auto got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
        while(got.status().code() == absl::StatusCode::kUnavailable) {
            got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
        }
        // ROME_CHECK_OK(ROME_RETURN(got.status()), got);

        // From there, decode the data into a value
        remote_plist iht_root = decltype(iht_root)(host.id, got->raddr());
        remote_plist value_ptr = pool_.Read<PList>(iht_root);
        this->root = value_ptr;
    }
}
