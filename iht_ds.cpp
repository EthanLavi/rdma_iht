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
                std::unique_ptr<MemoryPool::cm_type> cm) 
                : self_(self), pool_(self, std::move(cm)) {}

typedef remote_ptr<Secret> data;

void RdmaIHT::Init(MemoryPool::Peer host, const std::vector<MemoryPool::Peer> &peers) {
    int secret = 0x8888;
    is_host_ = self_.id == host.id;
    uint32_t block_size = 1 << 20;

    std::cout << "Vector length" << peers.size() << std::endl;
    auto status = pool_.Init(block_size, peers);
    std::cout << "Created pool" << std::endl;
    // ROME_CHECK_OK(ROME_RETURN(status), status);

    if (is_host_){
        std::cout << "Entering host branch" << std::endl;
        // Host machine, it is my responsibility to initiate configuration

        // Allocate data in pool
		RemoteObjectProto proto;
        data secret_ptr = pool_.Allocate<Secret>();
        secret_ptr->value = secret;
        proto.set_raddr(secret_ptr.address());

        // Iterate through peers
        for (auto p = peers.begin(); p != peers.end(); p++){
            std::cout << "Forming connection with " << p->address << std::endl;
            // Form a connection with the machine
            auto conn_or = pool_.connection_manager()->GetConnection(p->id);
            // ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);

            // Send the proto over
            status = conn_or.value()->channel()->Send(proto);
            // ROME_CHECK_OK(ROME_RETURN(status), status);
        }
    } else {
        std::cout << "Entering peer branch" << std::endl;
        std::cout << "Forming connection with " << host.address << std::endl;

        // Listen for a connection
        auto conn_or = pool_.connection_manager()->GetConnection(host.id);
        // ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);

        // Try to get the data from the machine, repeatedly trying until successful
        auto got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
        std::cout << "Before loop" << std::endl;
        while(got.status().code() == absl::StatusCode::kUnavailable) {
            got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
        }
        std::cout << "After loop" << std::endl;
        // ROME_CHECK_OK(ROME_RETURN(got.status()), got);

        // From there, decode the data into a value
        data secret_ptr = decltype(secret_ptr)(host.id, got->raddr());
        data value_ptr = pool_.Read<Secret>(secret_ptr);
        Secret data_in = *std::to_address(value_ptr);
        int value = data_in.value;

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


