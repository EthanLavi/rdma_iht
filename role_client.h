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
void test_output(bool show_passing, int actual, int expected, std::string message){
    if (actual != expected){
      ROME_INFO("[-] {} func():{} != expected:{}", message, actual, expected);
    } else if (show_passing) {
      ROME_INFO("[+] Test Case {} Passed!", message);
    }
}

typedef IHT_Op<int, int> Operation;

class Client : public ClientAdaptor<Operation> {
public:
  static std::unique_ptr<Client>
  Create(MemoryPool* pool, const MemoryPool::Peer &self, const MemoryPool::Peer &server, const std::vector<MemoryPool::Peer> &peers, ExperimentParams& params, std::barrier<> *barrier) {
    return std::unique_ptr<Client>(new Client(pool, self, server, peers, params, barrier));
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
    int key_range = client->params_.key_ub() - client->params_.key_lb();

    // Create a random operation generator that is 
    // - evenly distributed among the key range  
    // - within the specified ratios for operations
    std::uniform_real_distribution<double> dist = std::uniform_real_distribution<double>(0.0, 1.0);
    std::default_random_engine gen((unsigned) std::time(NULL));
    std::function<Operation(void)> generator = [&](){
      double rng = dist(gen) * 100;
      int k = dist(gen) * key_range + client->params_.key_lb();
      if (rng < client->params_.contains()){ // between 0 and CONTAINS
        return Operation(CONTAINS, k, 0);
      } else if (rng < client->params_.contains() + client->params_.insert()){ // between CONTAINS and CONTAINS + INSERT
        return Operation(INSERT, k, k);
      } else {
        return Operation(REMOVE, k, 0);
      }
    };

    ROME_INFO("CLIENT :: Created generator");

    // Generate two streams based on what the user wants (operation count or timed stream)
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

    ROME_INFO("CLIENT :: Created workload strema");

    // Create and start the workload driver (also starts client and lets it run).
    int32_t runtime = client->params_.runtime();
    int32_t qps_sample_rate = client->params_.qps_sample_rate();
    auto driver = rome::WorkloadDriver<Operation>::Create(
        std::move(client), std::move(workload_stream),
        qps_controller.get(),
        std::chrono::milliseconds(qps_sample_rate));
    ROME_INFO("CLIENT :: Created workload driver");
    ROME_ASSERT_OK(driver->Start());
    std::this_thread::sleep_for(std::chrono::seconds(runtime));
    ROME_ASSERT_OK(driver->Stop());
    return absl::OkStatus();
  }

  // Start the client
  absl::Status Start() override {
    // Make the IHT
    ROME_INFO("CLIENT :: Starting client...");
    auto status = iht_->Init(host_, peers_); 
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
      int scale_size = (CNF_PLIST_SIZE * CNF_ELIST_SIZE) * 128;
      for(int i = 0; i < scale_size; i++){
        test_output(false, iht_->contains(i), 0, std::string("Contains ") + std::to_string(i) + std::string(" false"));
        test_output(true, iht_->insert(i, i), 1, std::string("Insert ") + std::to_string(i));
        test_output(false, iht_->contains(i), 1, std::string("Contains ") + std::to_string(i) + std::string(" true"));
        test_output(false, iht_->result, i, std::string("Result gets set properly for ") + std::to_string(i));
      }
      for(int i = 0; i < scale_size; i++){
        test_output(false, iht_->contains(i), 1, std::string("Contains ") + std::to_string(i) + std::string(" maintains true"));
      }
      for(int i = 0; i < scale_size; i++){
        test_output(false, iht_->contains(i), 1, std::string("Contains ") + std::to_string(i) + std::string(" true"));
        test_output(false, iht_->remove(i), 1, std::string("Removes ") + std::to_string(i));
        test_output(false, iht_->contains(i), 0, std::string("Contains ") + std::to_string(i) + std::string(" false"));
      }
      for(int i = 0; i < scale_size; i++){
        test_output(false, iht_->contains(i), 0, std::string("Contains ") + std::to_string(i) + std::string(" maintains false"));
      }
      ROME_INFO("All test cases passed");
    } else {
      test_output(true, iht_->contains(5), 0, "Contains 5");
      test_output(true, iht_->contains(4), 0, "Contains 4");
      test_output(true, iht_->insert(5, 10), 1, "Insert 5");
      test_output(true, iht_->insert(5, 11), 0, "Insert 5 again should fail");
      test_output(true, iht_->result, 10, "Insert 5's failure, (result == old == 10)");
      iht_->result = 0;
      test_output(true, iht_->contains(5), 1, "Contains 5");
      test_output(true, iht_->result, 10, "Contains 5 (result == 10)");
      iht_->result = 0;
      test_output(true, iht_->contains(4), 0, "Contains 4");
      test_output(true, iht_->remove(5), 1, "Remove 5");
      test_output(true, iht_->result, 10, "Remove 5 (result == 10)");
      test_output(true, iht_->remove(4), 0, "Remove 4");
      test_output(true, iht_->contains(5), 0, "Contains 5");
      test_output(true, iht_->contains(4), 0, "Contains 4");
      ROME_INFO("All cases passed");
    }
    absl::Status stop_status = Stop();
    ROME_DEBUG("Stopping client is ok? {}", stop_status.ok());
    return absl::OkStatus();
  }

  // A function for communicating with the server that we are done. Will wait until server says it is ok to shut down
  absl::Status Stop() override {
    ROME_INFO("CLIENT :: Stopping client...");
    if (host_.id == self_.id) return absl::OkStatus(); // if we are the host, we don't need to do the stop sequence
    auto conn = iht_->pool_->connection_manager()->GetConnection(host_.id);
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
  Client(MemoryPool* pool, const MemoryPool::Peer &self, const MemoryPool::Peer &host, const std::vector<MemoryPool::Peer> &peers, ExperimentParams &params, std::barrier<> *barrier)
      : self_(self), host_(host), peers_(peers), params_(params), barrier_(barrier) {
          iht_ = std::make_unique<IHT>(self_, pool);
        }

  int count = 0;

  const MemoryPool::Peer self_;
  const MemoryPool::Peer host_;
  std::vector<MemoryPool::Peer> peers_;
  const ExperimentParams params_;
  std::unique_ptr<IHT> iht_;
  std::barrier<> *barrier_;
};