#include <algorithm>

#include "iht_ds.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"

using ::rome::rdma::MemoryPool;

// Function to run a test case
/*void test_output(int actual, int expected, std::string message){
    if (actual != expected){
      ROME_INFO("{} func():{} != expected:{}", message, actual, expected);
    } else {
      ROME_INFO("Test Case {} Passed!", message);
    }
}*/

class Server {
public:
  ~Server() = default;

  static std::unique_ptr<Server> Create(MemoryPool::Peer server, std::vector<MemoryPool::Peer> clients, struct config confs) {
    return std::unique_ptr<Server>(new Server(server, clients, confs));
  }

  absl::Status Launch(volatile bool *done, int runtime_s) {
    ROME_INFO("Starting server...");
    // Starts Connection Manager and connects to peers
    // auto status = iht_->Init(self_, peers_);
    // ROME_CHECK_OK(ROME_RETURN(status), status);
    ROME_INFO("We initialized the iht!");
    return absl::OkStatus();

    /*
    // Sleep while clients are running if there is a set runtime.
    if (runtime_s > 0) {
      auto runtime = std::chrono::seconds();
      std::this_thread::sleep_for(runtime);
      *done = true; // Just run once
    }

    // Wait for all clients to be done.
    for (auto &p : peers_) {
      auto conn_or = pool_.connection_manager()->GetConnection(p.id);
      if (!conn_or.ok())
        return conn_or.status();

      auto *conn = conn_or.value();
      auto msg = conn->channel()->TryDeliver<AckProto>();
      while ((!msg.ok() &&
              msg.status().code() == absl::StatusCode::kUnavailable)) {
        msg = conn->channel()->TryDeliver<AckProto>();
      }
    }
    return absl::OkStatus();
    */
  }

  /// Launch test on loopback
  absl::Status LaunchTestLoopback() {
    ROME_INFO("Starting server...");
    // Starts Connection Manager and connects to peers
    iht_ = std::make_unique<RdmaIHT>(self_, std::move(cm_), confs_);
    ROME_INFO("IHT Address Created");
    auto status = iht_->Init(self_, peers_); 
    ROME_INFO("Status: {}", status.ok());
    ROME_CHECK_OK(ROME_RETURN(status), status);
    ROME_INFO("We initialized the iht!");
    return absl::OkStatus();
  }

private:
  Server(MemoryPool::Peer self, std::vector<MemoryPool::Peer> peers, struct config confs)
      : self_(self), peers_(peers), confs_(confs) {
        cm_ = std::make_unique<MemoryPool::cm_type>(self.id);
      }

  const MemoryPool::Peer self_;
  std::vector<MemoryPool::Peer> peers_;
  std::unique_ptr<MemoryPool::cm_type> cm_;
  std::unique_ptr<RdmaIHT> iht_;
  struct config confs_;
};