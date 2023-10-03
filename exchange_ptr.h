#pragma once

#include <cstdint>
#include <atomic>

#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/rdma_memory.h"
#include "rome/logging/logging.h"
#include "common.h"

using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;

namespace tcp {
/// @brief Initialize the IHT by connecting to the peers and exchanging the PList pointer
/// @param self the node running this function
/// @param host the leader of the initialization
/// @param peers all the nodes in the neighborhood
/// @param root_ptr the pointer to exchange
/// @return the root pointer we got from the exchange
remote_ptr<anon_ptr> ExchangePointer(EndpointContext ctx, MemoryPool::Peer self, MemoryPool::Peer host, remote_ptr<anon_ptr> root_ptr){
  bool is_host_ = self.id == host.id;

  if (is_host_){
    // Iterate through peers
    tcp::message ptr_message = tcp::message(root_ptr.address());
    tcp::SocketManager* socket_handle = tcp::SocketManager::getInstance();
    // Send the address over
    socket_handle->send_to_all(&ptr_message);
    return root_ptr;
  } else {
    // sleep for a short while to ensure the receiving end is up and running
    std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
    // Form the connection
    tcp::EndpointManager* endpoint = tcp::EndpointManager::getInstance(ctx, host.address.c_str());
    // Get the message
    tcp::message ptr_message;
    endpoint->recv_server(&ptr_message);

    // From there, decode the data into a value
    return remote_ptr<anon_ptr>(host.id, ptr_message.get_first());
  }
}
}