#pragma once

#include <barrier>
#include <chrono>
#include <cstdlib>

#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/colosseum/client_adaptor.h"
#include "rome/colosseum/streams/streams.h"
#include "rome/colosseum/qps_controller.h"
#include "rome/colosseum/workload_driver.h"
#include "rome/util/clocks.h"
#include "iht_ds.h"
#include "operation.h"
#include "config.h"
#include "proto/experiment.pb.h"

using ::rome::rdma::MemoryPool;
using ::rome::ClientAdaptor;
using ::rome::WorkloadDriver;

typedef RdmaIHT<int, int, CNF_ELIST_SIZE, CNF_PLIST_SIZE> IHT;

// Function to run a test case
void test_output(int actual, int expected, std::string message){
    if (actual != expected){
      ROME_INFO("[-] {} func():{} != expected:{}", message, actual, expected);
    } else {
      ROME_INFO("[+] Test Case {} Passed!", message);
    }
}

typedef IHT_Op<int, int> Operation;

class Client : public ClientAdaptor<Operation> {
public:
  static std::unique_ptr<Client>
  Create(const MemoryPool::Peer &self, const MemoryPool::Peer &server, const std::vector<MemoryPool::Peer> &peers, ExperimentParams& params, std::barrier<> *barrier) {
    return std::unique_ptr<Client>(new Client(self, server, peers, params, barrier));
  }

  static absl::Status Run(std::unique_ptr<Client> client, volatile bool *done) {
    // TODO: Signal Handler
    // signal(SIGINT, signal_handler);
    
    // Setup qps_controller.
    std::unique_ptr<rome::LeakyTokenBucketQpsController<util::SystemClock>>
        qps_controller =
          rome::LeakyTokenBucketQpsController<util::SystemClock>::Create(client->params_.max_qps_second()); // what is the value here

    // auto *client_ptr = client.get();
    std::vector<Operation> operations = std::vector<Operation>();
    
    // initialize random number generator and key_range
    std::srand((unsigned) std::time(NULL));
    int key_range = client->params_.key_ub() - client->params_.key_lb();

    std::function<Operation(void)> generator = [&](){
      int rng = std::rand() % 100;
      int k = (std::rand() / RAND_MAX) * key_range + client->params_.key_lb();
      if (rng < client->params_.contains()){ // between 0 and CONTAINS
        return Operation(CONTAINS, k, 0);
      } else if (rng < client->params_.contains() + client->params_.insert()){ // between CONTAINS and CONTAINS + INSERT
        return Operation(INSERT, k, k);
      } else {
        return Operation(REMOVE, k, 0);
      }
    };

    // Populate the data structure
    for (int i = 0; i < key_range / 2; i++){
      operations.push_back(generator());
    }

    
    std::unique_ptr<rome::Stream<Operation>> workload_stream;
    if(client->params_.unlimited_stream()){
      workload_stream = std::make_unique<rome::EndlessStream<Operation>>(generator);
    } else {
      // Deliver a workload
      int WORKLOAD_AMOUNT = client->params_.op_count();
      for(int j = 0; j < WORKLOAD_AMOUNT; j++){
        operations.push_back(generator());
      }
      workload_stream = std::make_unique<rome::TestStream<Operation>>(operations);
    }

    // Create and start the workload driver (also starts client).
    auto driver = rome::WorkloadDriver<Operation>::Create(
        std::move(client), std::move(workload_stream),
        qps_controller.get(),
        std::chrono::milliseconds(client->params_.qps_sample_rate()));
    ROME_ASSERT_OK(driver->Start());
    std::this_thread::sleep_for(std::chrono::seconds(client->params_.runtime()));
    ROME_ASSERT_OK(driver->Stop());
    return absl::OkStatus();
  }

  // Start the client
  absl::Status Start() override {
    // Make the IHT
    ROME_INFO("Starting client...");
    auto status = iht_->Init(host_, peers_, params_.region_size()); 
    ROME_CHECK_OK(ROME_RETURN(status), status);
    // Conditional to allow us to bypass the barrier for certain client types
    if (barrier_ != nullptr) barrier_->arrive_and_wait();
    return status;
  }

  // Runs the next operation
  absl::Status Apply(const Operation &op) override {
    count++;
    
    switch (op.op_type){
      case(CONTAINS):
        ROME_INFO("Running Operation: contains({})", op.key);
        iht_->contains(op.key);
        break;
      case(INSERT):
        ROME_INFO("Running Operation: insert({}, {})", op.key, op.value);
        iht_->insert(op.key, op.value);
        break;
      case(REMOVE):
        ROME_INFO("Running Operation: remove({})", op.key);
        iht_->remove(op.key);
        break;
      default:
        ROME_INFO("Expected CONTAINS, INSERT, or REMOVE operation.");
        break;
    }
    // Think in between operations for simulation purposes. 
    if (params_.has_think_time() && params_.think_time() != 0){
      auto start = util::SystemClock::now();
      while (util::SystemClock::now() - start < std::chrono::nanoseconds(params_.think_time()));
    }
    return absl::OkStatus();
  }

  /// @brief Runs single-client silent-server test cases on the iht
  /// @param at_scale is true for testing at scale (+10,000 operations)
  /// @return OkStatus if everything worked. Otherwise will shutdown the client.
  absl::Status Operations(bool at_scale){
    absl::Status init_status = Start();
    ROME_DEBUG("Init client is ok? {}", init_status.ok());
    if (at_scale){
      int scale_size = (CNF_PLIST_SIZE * CNF_ELIST_SIZE) * 2;
      for(int i = 0; i < scale_size; i++){
        test_output(iht_->contains(i), 0, "Contains i false");
        test_output(iht_->insert(i, i), 1, "Insert i");
        test_output(iht_->contains(i), 1, "Contains i true");
        test_output(iht_->result, i, "Result gets set properly");
      }
      for(int i = 0; i < scale_size; i++){
        test_output(iht_->contains(i), 1, "Contains i maintains true");
      }
      for(int i = 0; i < scale_size; i++){
        test_output(iht_->contains(i), 1, "Contains i true");
        test_output(iht_->remove(i), 1, "Removes i");
        test_output(iht_->contains(i), 0, "Contains i false");
      }
      for(int i = 0; i < scale_size; i++){
        test_output(iht_->contains(i), 0, "Contains i maintains false");
      }
      ROME_INFO("All test cases passed");
    } else {
      test_output(iht_->contains(5), 0, "Contains 5");
      test_output(iht_->contains(4), 0, "Contains 4");
      test_output(iht_->insert(5, 10), 1, "Insert 5");
      test_output(iht_->insert(5, 11), 0, "Insert 5 again should fail");
      test_output(iht_->result, 10, "Insert 5's failure, (result == old == 10)");
      iht_->result = 0;
      test_output(iht_->contains(5), 1, "Contains 5");
      test_output(iht_->result, 10, "Contains 5 (result == 10)");
      iht_->result = 0;
      test_output(iht_->contains(4), 0, "Contains 4");
      test_output(iht_->remove(5), 1, "Remove 5");
      test_output(iht_->result, 10, "Remove 5 (result == 10)");
      test_output(iht_->remove(4), 0, "Remove 4");
      test_output(iht_->contains(5), 0, "Contains 5");
      test_output(iht_->contains(4), 0, "Contains 4");
      ROME_INFO("All cases passed");
    }
    absl::Status stop_status = Stop();
    ROME_DEBUG("Stopping client is ok? {}", stop_status.ok());
    return absl::OkStatus();
  }

  // A function for communicating with the server that we are done. Will wait until server says it is ok to shut down
  absl::Status Stop() override {
    ROME_INFO("Stopping client...");
    auto conn = iht_->pool_.connection_manager()->GetConnection(host_.id);
    ROME_CHECK_OK(ROME_RETURN(util::InternalErrorBuilder() << "Failed to retrieve server connection"), conn);
    AckProto e;
    
    auto sent = conn.value()->channel()->Send(e); // send the ack to let the server know that we are done

    // Wait to receive an ack back. Letting us know that the other clients are done.
    auto msg = conn.value()->channel()->TryDeliver<AckProto>();
    while ((!msg.ok() && msg.status().code() == absl::StatusCode::kUnavailable)) {
        msg = conn.value()->channel()->TryDeliver<AckProto>();
    }

    // Return ok status
    return absl::OkStatus();
  }

private:
  Client(const MemoryPool::Peer &self, const MemoryPool::Peer &host, const std::vector<MemoryPool::Peer> &peers, ExperimentParams &params, std::barrier<> *barrier)
      : self_(self), host_(host), peers_(peers), params_(params), barrier_(barrier) {
          iht_ = std::make_unique<IHT>(self_, std::make_unique<MemoryPool::cm_type>(self.id));
        }

  int count = 0;

  const MemoryPool::Peer self_;
  const MemoryPool::Peer host_;
  std::vector<MemoryPool::Peer> peers_;
  const ExperimentParams params_;
  std::unique_ptr<IHT> iht_;
  std::barrier<> *barrier_;
};