#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <infiniband/verbs.h>
#include <cstdio>
#include <iostream>

#include "../rome/rdma/memory_pool/remote_ptr.h"
#include "../rome/rdma/connection_manager/connection_manager.h"
#include "iht_ds.h"

using rome::rdma::ConnectionManager;
using rome::rdma::MemoryPool;
using rome::rdma::remote_nullptr;
using rome::rdma::remote_ptr;
using rome::rdma::RemoteObjectProto;

RdmaIHT::RdmaIHT(MemoryPool::Peer self, std::unique_ptr<MemoryPool::cm_type> cm) : self_(std::move(self)), pool_(self, std::move(cm)) {}

typedef remote_ptr<int> data;

void RdmaIHT::Init(MemoryPool::Peer host, const std::vector<MemoryPool::Peer> &peers) {
    int secret = 0x8888;
    is_host_ = self_.id == host.id;
    uint32_t block_size = 1 << 20;
    auto status = pool_.Init(block_size, peers);

    if (is_host_){
        // Host machine, it is my responsibility to initiate configuration

        // Allocate data in pool
		RemoteObjectProto proto;
        remote_ptr<data> secret_ptr = pool_.Allocate<data>();
        proto.set_raddr(secret_ptr.address());

        // Iterate through peers
        for (auto p = peers.begin(); p != peers.end(); p++){
            // Form a connection with the machine
            auto conn_or = pool_.connection_manager()->GetConnection(p->id);

            // Send the proto over
            status = conn_or.value()->channel()->Send(proto);
        }
    } else {
        // Not host, listen

        // Listen for a connection
        auto conn_or = pool_.connection_manager()->GetConnection(host.id);

        // Try to get the data from the machine, repeatedly trying until successful
        RemoteObjectProto got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
        while(got.status().code() == absl::StatusCode::kUnavailable) {
            got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
        }

        // From there, decode the data into a value
        remote_ptr<data> secret_ptr;
        secret_ptr = decltype(secret_ptr)(host.id, got->raddr());
        data value_ptr = pool_.Read<data>(secret_ptr);
        int value = pool_.Read<int>(value_ptr);

        freopen("output.txt", "w", stdout);
        std::cout << value << std::endl;
    }
}

bool RdmaIHT::contains() {
    return false;
}

bool RdmaIHT::insert() {
    return false;
}

bool RdmaIHT::remove() {
    return false;
}


