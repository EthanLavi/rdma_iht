#include <algorithm>

#include "iht_ds.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "config.h"
#include "protos/experiment.pb.h"

using ::rome::rdma::MemoryPool;

typedef RdmaIHT<int, int, CNF_ELIST_SIZE, CNF_PLIST_SIZE> IHT;

class Server {
public:
  ~Server() = default;

  static std::unique_ptr<Server> Create(MemoryPool::Peer server, std::vector<MemoryPool::Peer> clients, ExperimentParams params) {
    return std::unique_ptr<Server>(new Server(server, clients, params));
  }

  /// @brief Start the server
  /// @param pool the memory pool to use
  /// @param done a bool for inter-thread communication
  /// @param runtime_s how long to wait before listening for finishing messages
  /// @return the status
  absl::Status Launch(MemoryPool* pool, volatile bool *done, int runtime_s) {
    ROME_INFO("SERVER :: Starting server...");
    // Starts Connection Manager and connects to peers
    iht_ = std::make_unique<IHT>(self_, pool);
    auto status = iht_->Init(self_, peers_);
    ROME_CHECK_OK(ROME_RETURN(status), status);

    // Sleep while clients are running if there is a set runtime.
    if (runtime_s > 0) {
      ROME_INFO("SERVER :: Sleeping for {}", runtime_s);
      // We sleep for an extra 2 seconds to let the client populate the data structure
      std::this_thread::sleep_for(std::chrono::seconds(runtime_s));
    }

    // Sync with the clients
    while(!done){
      ROME_INFO("Inside !done");
      // Sleep for a half second while not done
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Wait for all clients to be done.
    for (auto &p : peers_) {
      if (p.id == self_.id) continue; // ignore self since joining threads will force client and server to end at the same time
      ROME_INFO("SERVER :: receiving ack to {}", p.id);
      auto conn_or = iht_->pool_->connection_manager()->GetConnection(p.id);
      if (!conn_or.ok())
        return conn_or.status();

      auto *conn = conn_or.value();
      auto msg = conn->channel()->TryDeliver<AckProto>();
      while ((!msg.ok() && msg.status().code() == absl::StatusCode::kUnavailable)) {
        msg = conn->channel()->TryDeliver<AckProto>();
      }
    }

    // Let all clients know that we are done
    for (auto &p : peers_) {
      if (p.id == self_.id) continue; // ignore self since joining threads will force client and server to end at the same time
      ROME_INFO("SERVER :: sending ack from {}", p.id);
      auto conn_or = iht_->pool_->connection_manager()->GetConnection(p.id);
      if (!conn_or.ok())
        return conn_or.status();
      auto *conn = conn_or.value();
      AckProto e;
      // Send back an ack proto let the client know that all the other clients are done
      auto sent = conn->channel()->Send(e);
    }
    return absl::OkStatus();
  }

private:
  Server(MemoryPool::Peer self, std::vector<MemoryPool::Peer> peers, ExperimentParams params)
      : self_(self), peers_(peers), params_(params) {
        cm_ = std::make_unique<MemoryPool::cm_type>(self.id);
      }

  const MemoryPool::Peer self_;
  std::vector<MemoryPool::Peer> peers_;
  const ExperimentParams params_;
  std::unique_ptr<MemoryPool::cm_type> cm_;
  std::unique_ptr<IHT> iht_;
};