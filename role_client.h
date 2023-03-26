#pragma once

#include <barrier>
#include <chrono>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include "rome/colosseum/client_adaptor.h"
#include "rome/colosseum/streams/streams.h"
#include "rome/colosseum/qps_controller.h"
#include "rome/colosseum/workload_driver.h"

#include "rome/util/clocks.h"

#include "iht_ds.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"

using ::rome::rdma::MemoryPool;
using ::rome::ClientAdaptor;
using ::rome::WorkloadDriver;

// Function to run a test case
void test_output(int actual, int expected, std::string message){
    if (actual != expected){
      ROME_INFO("[-] {} func():{} != expected:{}", message, actual, expected);
      exit(1);
    } else {
      ROME_INFO("[+] Test Case {} Passed!", message);
    }
}

class Client : public ClientAdaptor<rome::NoOp> {
public:
  static std::unique_ptr<Client>
  Create(const MemoryPool::Peer &self, const MemoryPool::Peer &server, const std::vector<MemoryPool::Peer> &peers) {
    return std::unique_ptr<Client>(new Client(self, server, peers));
  }
  
  static void signal_handler(int signal) { 
    ROME_INFO("SIGNAL: ", signal, " HANDLER!!!\n");
    // TODO: SHould be called with a driver but not sure how to move ownership of ptr..
    // this->Stop();
    // Wait for all clients to be done shutting down
    std::this_thread::sleep_for(std::chrono::seconds(5));
    exit(1);
  }

  /*static absl::Status Run(std::unique_ptr<Client> client, volatile bool *done) {
    //Signal Handler
    signal(SIGINT, signal_handler);
    
    // Setup qps_controller.
    std::unique_ptr<rome::LeakyTokenBucketQpsController<util::SystemClock>>
        qps_controller =
          rome::LeakyTokenBucketQpsController<util::SystemClock>::Create(-1);
    

    // auto *client_ptr = client.get();

    // Create and start the workload driver (also starts client).
    auto driver = rome::WorkloadDriver<rome::NoOp>::Create(
        std::move(client), std::make_unique<rome::NoOpStream>(),
        qps_controller.get(),
        std::chrono::milliseconds(10));
    ROME_ASSERT_OK(driver->Start());
    
    std::this_thread::sleep_for(std::chrono::seconds(10));
    //Just sleep here for 10 seconds
    ROME_INFO("Stopping client...");
    ROME_ASSERT_OK(driver->Stop());

    // Output results.
    // ResultProto result;
    // result.mutable_experiment_params()->CopyFrom(experiment_params);
    // result.mutable_client()->CopyFrom(client_ptr->ToProto());
    // result.mutable_driver()->CopyFrom(driver->ToProto());

    // Sleep for a hot sec to let the server receive the messages sent by the
    // clients before disconnecting.
    // (see https://github.com/jacnel/project-x/issues/15)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return absl::OkStatus();
  }*/

  static absl::Status Run(std::unique_ptr<Client> client, volatile bool *done) {}

  // Start the client
  absl::Status Start() override {
    // Make the IHT
    ROME_INFO("Starting client...");
    // barrier_->arrive_and_wait(); //waits for all clients to get lock Initialized, addr from host
    auto status = iht_->Init(host_, peers_); 
    ROME_CHECK_OK(ROME_RETURN(status), status);
    return status;
  }

  // Runs the next operation
  absl::Status Apply(const rome::NoOp &op) override {
    count++;
    switch (count){
      case 1:
        test_output(iht_->contains(5), 0, "Contains 5");
        break;
      case 2:
        test_output(iht_->contains(4), 0, "Contains 4");
        break;
      case 3:
        test_output(iht_->insert(5), 1, "Insert 5");
        break;
      case 4:
        test_output(iht_->contains(5), 1, "Contains 5");
        break;
      case 5:
        test_output(iht_->contains(4), 0, "Contains 4");
        break;
      case 6:
        test_output(iht_->remove(5), 1, "Remove 5");
        break;
      case 7:
        test_output(iht_->contains(5), 0, "Contains 5");
        break;
      case 8:
        test_output(iht_->contains(4), 0, "Contains 4");
        break;
      case 9:
        ROME_INFO("All cases passed");
    }

    return absl::OkStatus();
  }

  absl::Status Operations(){
    test_output(iht_->insert(5), 1, "Insert 5");
    test_output(iht_->contains(5), 1, "Contains 5");
    test_output(iht_->contains(4), 0, "Contains 4");
    test_output(iht_->remove(5), 1, "Remove 5");
    test_output(iht_->contains(5), 0, "Contains 5");
    test_output(iht_->contains(4), 0, "Contains 4");
    ROME_INFO("All cases passed");

    return absl::OkStatus();
  }

// Not implemented yet
  absl::Status Stop() override {
    // TODO: IMPLEMENT
    return absl::OkStatus();
  }

private:
  Client(const MemoryPool::Peer &self, const MemoryPool::Peer &host, const std::vector<MemoryPool::Peer> &peers)
      : self_(self), host_(host), 
        peers_(peers) {
          struct config confs{8, 128};
          iht_ = std::make_unique<RdmaIHT>(self_, std::make_unique<MemoryPool::cm_type>(self.id), confs);
        }

  int count = 0;

  const MemoryPool::Peer self_;
  const MemoryPool::Peer host_;
  std::vector<MemoryPool::Peer> peers_;
  std::unique_ptr<RdmaIHT> iht_;
  // std::barrier<> *barrier_;
};