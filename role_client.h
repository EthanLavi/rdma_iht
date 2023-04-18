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
using ::rome::rdma::RemoteObjectProto;

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
  Create(const MemoryPool::Peer &self, const MemoryPool::Peer &server, const std::vector<MemoryPool::Peer> &peers, const ExperimentParams &params) {
    return std::unique_ptr<Client>(new Client(self, server, peers, params));
  }
  
  static void signal_handler(int signal) { 
    ROME_INFO("SIGNAL: ", signal, " HANDLER!!!\n");
    // TODO: SHould be called with a driver but not sure how to move ownership of ptr..
    // this->Stop();
    // Wait for all clients to be done shutting down
    std::this_thread::sleep_for(std::chrono::seconds(5));
    exit(1);
  }

  static absl::Status Run(std::unique_ptr<Client> client, volatile bool *done) {
    //Signal Handler
    signal(SIGINT, signal_handler);

    ExperimentParams exp_params = client->params_;
    
    // Setup qps_controller.
    std::unique_ptr<rome::LeakyTokenBucketQpsController<util::SystemClock>>
        qps_controller =
          rome::LeakyTokenBucketQpsController<util::SystemClock>::Create(exp_params.max_qps_second()); // what is the value here

    // auto *client_ptr = client.get();
    std::vector<Operation> operations = std::vector<Operation>();
    
    // TODO: Populate
    int i;
    for (i = 0; i < 100; i++){
      operations.push_back({INSERT, i, i});
    }

    // initialize random number generator
    std::srand((unsigned) std::time(NULL));

    // Deliver a workload
    int WORKLOAD_AMOUNT = exp_params.op_count();
    for(; i < WORKLOAD_AMOUNT; i++){
      int rng = std::rand() % 100;
      if (rng < exp_params.contains()){ // between 0 and CONTAINS
        operations.push_back({CONTAINS, i, 0});
      } else if (rng < exp_params.contains() + exp_params.insert()){ // between CONTAINS and CONTAINS + INSERT
        operations.push_back({INSERT, i, i});
      } else {
        operations.push_back({REMOVE, i, 0});
      }
    }
    
    std::unique_ptr<rome::Stream<Operation>> workload_stream = std::make_unique<rome::TestStream<Operation>>(operations);

    // Create and start the workload driver (also starts client).
    auto driver = rome::WorkloadDriver<Operation>::Create(
        std::move(client), std::move(workload_stream),
        qps_controller.get(),
        std::chrono::milliseconds(exp_params.qps_sample_rate()));
    ROME_ASSERT_OK(driver->Start());
    std::this_thread::sleep_for(std::chrono::seconds(exp_params.runtime()));
    ROME_ASSERT_OK(driver->Stop());
    return absl::OkStatus();
  }

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
  // TODO: Make this function do bulk operations.
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
  /// @return OkStatus if everything worked. Otherwise will shutdown the client.
  absl::Status Operations(){
    // TODO: Rewrite to test correctness of data structure
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
  Client(const MemoryPool::Peer &self, const MemoryPool::Peer &host, const std::vector<MemoryPool::Peer> &peers, const ExperimentParams &params)
      : self_(self), host_(host), params_(params), peers_(peers) {
          iht_ = std::make_unique<IHT>(self_, std::make_unique<MemoryPool::cm_type>(self.id));
        }

  int count = 0;

  const MemoryPool::Peer self_;
  const MemoryPool::Peer host_;
  const ExperimentParams params_;
  std::vector<MemoryPool::Peer> peers_;
  std::unique_ptr<IHT> iht_;
  // std::barrier<> *barrier_;
};