#include <algorithm>

#include "structures/iht_ds.h"
#include "structures/hashtable.h"
#include "structures/test_map.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "common.h"
#include "protos/experiment.pb.h"

using ::rome::rdma::MemoryPool;

typedef RdmaIHT<int, int, CNF_ELIST_SIZE, CNF_PLIST_SIZE> IHT;
// typedef Hashtable<int, int, CNF_PLIST_SIZE> IHT;
// typedef TestMap<int, int> IHT;

class Server {
public:
  ~Server() = default;

  static std::unique_ptr<Server> Create(MemoryPool::Peer server, std::vector<MemoryPool::Peer> clients, ExperimentParams params, MemoryPool* pool) {
    return std::unique_ptr<Server>(new Server(server, clients, params, pool));
  }

  /// @brief Start the server
  /// @param pool the memory pool to use
  /// @param done a bool for inter-thread communication
  /// @param runtime_s how long to wait before listening for finishing messages
  /// @return the status
  absl::Status Launch(volatile bool* done, int runtime_s, std::function<void()> cleanup) {
    // Sleep while clients are running if there is a set runtime.
    if (runtime_s > 0) {
      ROME_INFO("SERVER :: Sleeping for {}", runtime_s);

      for(int it = 0; it < runtime_s * 10; it++){
        // We sleep for 1 second, runtime times, and do intermitten cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Check for server related cleanup
        cleanup();
      }
    }

    // Sync with the clients
    while(!(*done)){
      // Sleep for 1/10 second while not done
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      // Check for server related cleanup
      cleanup();
    }

    // Wait for all clients to be done.
    if (pool_ == NULL) ROME_INFO("Using pool so I don't get compiler error while I test. Eventually I should commit to the tcp approach and remove pool");
    tcp::SocketManager* socket_handle = tcp::SocketManager::getInstance();
    tcp::message recv_buffer[socket_handle->num_clients()];
    socket_handle->recv_from_all(recv_buffer);
    ROME_INFO("SERVER :: received ack");
    
    tcp::message send_buffer;
    socket_handle->send_to_all(&send_buffer);
    ROME_INFO("SERVER :: sent ack");
    return absl::OkStatus();
  }

private:
  Server(MemoryPool::Peer self, std::vector<MemoryPool::Peer> peers, ExperimentParams params, MemoryPool* pool)
      : self_(self), peers_(peers), params_(params), pool_(pool) {}

  const MemoryPool::Peer self_;
  std::vector<MemoryPool::Peer> peers_;
  const ExperimentParams params_;
  MemoryPool* pool_;
};