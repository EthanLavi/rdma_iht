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

#include "structures/hashtable.h"
#include "structures/iht_ds.h"
#include "structures/test_map.h"
#include "common.h"
#include "tcp.h"
#include "protos/experiment.pb.h"

using ::rome::rdma::MemoryPool;
using ::rome::ClientAdaptor;
using ::rome::WorkloadDriver;
using ::rome::WorkloadDriverProto;

typedef RdmaIHT<int, int, CNF_ELIST_SIZE, CNF_PLIST_SIZE> IHT;
// typedef Hashtable<int, int, CNF_PLIST_SIZE> IHT;
// typedef TestMap<int, int> IHT;

std::string fromStateValue(state_value value){
  if (FALSE_STATE == value){
    return std::string("FALSE");
  } else if (TRUE_STATE == value){
    return std::string("TRUE");
  } else if (REHASH_DELETED == value){
    return std::string("REHASH DELETED");
  } else {
    return std::string("UNKNOWN - ") + std::to_string(value);
  }
}

// Function to run a test case
void test_output(bool show_passing, HT_Res<int> actual, HT_Res<int> expected, std::string message){
    if (actual.status != expected.status && actual.result != expected.result){
      ROME_INFO("[-] {} func():({},{}) != expected:({},{})", message, fromStateValue(actual.status), actual.result, fromStateValue(expected.status), expected.result);
    } else if (show_passing) {
      ROME_INFO("[+] Test Case {} Passed!", message);
    }
}

typedef IHT_Op<int, int> Operation;

class Client : public ClientAdaptor<Operation> {
public:
  static std::unique_ptr<Client>
  Create(const MemoryPool::Peer &server, const tcp::EndpointContext &ctx, ExperimentParams& params, std::barrier<> *barrier, IHT* iht, bool master_client) {
    return std::unique_ptr<Client>(new Client(server, ctx, params, barrier, iht, master_client));
  }

  /// @brief Run the client
  /// @param client the client instance to run with
  /// @param done a volatile bool for inter-thread communication
  /// @param frac if 0, won't populate. Otherwise, will do this fraction of the population
  /// @return the resultproto
  static absl::StatusOr<WorkloadDriverProto> Run(std::unique_ptr<Client> client, volatile bool *done, double frac) {
    int key_lb = client->params_.key_lb(), key_ub = client->params_.key_ub();
    int op_count = (key_ub - key_lb) * frac;
    ROME_INFO("CLIENT :: Data structure ({}%) is being populated ({} items inserted) by this client", frac * 100, op_count);
    client->iht_->pool_->RegisterThread();
    client->iht_->populate(op_count, key_lb, key_ub, [=](int key){ return key; });
    ROME_INFO("CLIENT :: Done with populate!");

    // TODO: Sleeping for 1 second to account for difference between remote client start times. Must fix this in the future to a better solution
    // The idea is even though remote nodes won't start workload at the same time, at least the data structure is somewhat guaranteed to be populated
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // TODO: Signal Handler
    // signal(SIGINT, signal_handler);
    
    // Setup qps_controller.
    std::unique_ptr<rome::LeakyTokenBucketQpsController<util::SystemClock>>
        qps_controller =
          rome::LeakyTokenBucketQpsController<util::SystemClock>::Create(client->params_.max_qps_second()); // what is the value here

    std::vector<Operation> operations = std::vector<Operation>();
    
    // initialize random number generator and key_range
    int key_range = client->params_.key_ub() - client->params_.key_lb();

    // Create a random operation generator that is 
    // - evenly distributed among the key range  
    // - within the specified ratios for operations
    std::uniform_real_distribution<double> dist = std::uniform_real_distribution<double>(0.0, 1.0);
    std::default_random_engine gen((unsigned) std::time(NULL));
    int lb = client->params_.key_lb();
    int contains = client->params_.contains();
    int insert = client->params_.insert();
    std::function<Operation(void)> generator = [&](){
      double rng = dist(gen) * 100;
      int k = dist(gen) * key_range + lb;
      if (rng < contains){ // between 0 and CONTAINS
        return Operation(CONTAINS, k, 0);
      } else if (rng < contains + insert){ // between CONTAINS and CONTAINS + INSERT
        return Operation(INSERT, k, k);
      } else {
        return Operation(REMOVE, k, 0);
      }
    };

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

    // Create and start the workload driver (also starts client and lets it run).
    int32_t runtime = client->params_.runtime();
    int32_t qps_sample_rate = client->params_.qps_sample_rate();
    std::barrier<>* barr = client->barrier_;
    bool master_client = client->master_client_;
    barr->arrive_and_wait();
    auto driver = rome::WorkloadDriver<Operation>::Create(
        std::move(client), std::move(workload_stream),
        qps_controller.get(),
        std::chrono::milliseconds(qps_sample_rate));
    ROME_ASSERT_OK(driver->Start());
    std::this_thread::sleep_for(std::chrono::seconds(runtime));
    ROME_INFO("Done here, stop sequence");
    // Wait for all the clients to stop. Then set the done to true to release the server
    if (master_client){
      if (barr != nullptr) barr->arrive_and_wait();
      *done = true;
    }
    ROME_ASSERT_OK(driver->Stop());
    ROME_INFO("CLIENT :: Driver generated {}", driver->ToString());
    return driver->ToProto();
  }

  // Start the client
  absl::Status Start() override {
    ROME_INFO("CLIENT :: Starting client...");
    // Conditional to allow us to bypass the barrier for certain client types
    // We want to start at the same time
    this->iht_->pool_->RegisterThread();
    if (barrier_ != nullptr) barrier_->arrive_and_wait();
    return absl::OkStatus();
  }

  // Runs the next operation
  absl::Status Apply(const Operation &op) override {
    count++;
    HT_Res<int> res = HT_Res<int>(FALSE_STATE, 0);
    switch (op.op_type){
      case(CONTAINS):
        if (count % progression == 0) ROME_INFO("Running Operation {}: contains({})", count, op.key);
        // ROME_INFO("Running Operation {}: contains({})", count, op.key);
        res = iht_->contains(op.key);
        if (res.status == TRUE_STATE) ROME_ASSERT(res.result == op.key, "Invalid result of ({}) contains operation {}!={}", res.status, res.result, op.key);
        break;
      case(INSERT):
        if (count % progression == 0) ROME_INFO("Running Operation {}: insert({}, {})", count, op.key, op.value);
        // ROME_INFO("Running Operation {}: insert({}, {})", count, op.key, op.value);
        res = iht_->insert(op.key, op.value);
        break;
      case(REMOVE):
        if (count % progression == 0) ROME_INFO("Running Operation {}: remove({})", count, op.key);
        // ROME_INFO("Running Operation {}: remove({})", count, op.key);
        res = iht_->remove(op.key);
        if (res.status == TRUE_STATE) ROME_ASSERT(res.result == op.key, "Invalid result of ({}) remove operation {}!={}", res.status, res.result, op.key);
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
    if (at_scale){
      int scale_size = (CNF_PLIST_SIZE * CNF_ELIST_SIZE) * 8;
      bool show_passing = false;
      for(int i = 0; i < scale_size; i++){
        test_output(show_passing, iht_->contains(i), HT_Res<int>(FALSE_STATE, 0), std::string("Contains ") + std::to_string(i) + std::string(" false"));
        test_output(show_passing, iht_->insert(i, i), HT_Res<int>(TRUE_STATE, 0), std::string("Insert ") + std::to_string(i));
        test_output(show_passing, iht_->contains(i), HT_Res<int>(TRUE_STATE, i), std::string("Contains ") + std::to_string(i) + std::string(" true"));
      }
      ROME_INFO(" = 25% Finished = ");
      for(int i = 0; i < scale_size; i++){
        test_output(show_passing, iht_->contains(i), HT_Res<int>(TRUE_STATE, i), std::string("Contains ") + std::to_string(i) + std::string(" maintains true"));
      }
      ROME_INFO(" = 50% Finished = ");
      for(int i = 0; i < scale_size; i++){
        test_output(show_passing, iht_->remove(i), HT_Res<int>(TRUE_STATE, i), std::string("Removes ") + std::to_string(i));
        test_output(show_passing, iht_->contains(i), HT_Res<int>(FALSE_STATE, 0), std::string("Contains ") + std::to_string(i) + std::string(" false"));
      }
      ROME_INFO(" = 75% Finished = ");
      for(int i = 0; i < scale_size; i++){
        test_output(show_passing, iht_->contains(i), HT_Res<int>(FALSE_STATE, 0), std::string("Contains ") + std::to_string(i) + std::string(" maintains false"));
      }
      ROME_INFO("All test cases finished");
    } else {
      ROME_INFO("Starting test cases.");
      test_output(true, iht_->contains(5), HT_Res<int>(FALSE_STATE, 0), "Contains 5");
      test_output(true, iht_->contains(4), HT_Res<int>(FALSE_STATE, 0), "Contains 4");
      test_output(true, iht_->insert(5, 10), HT_Res<int>(FALSE_STATE, 0), "Insert 5");
      test_output(true, iht_->insert(5, 11), HT_Res<int>(FALSE_STATE, 10), "Insert 5 again should fail");
      test_output(true, iht_->contains(5), HT_Res<int>(TRUE_STATE, 10), "Contains 5");
      test_output(true, iht_->contains(4), HT_Res<int>(FALSE_STATE, 0), "Contains 4");
      test_output(true, iht_->remove(5), HT_Res<int>(TRUE_STATE, 10), "Remove 5");
      test_output(true, iht_->remove(4), HT_Res<int>(FALSE_STATE, 0), "Remove 4");
      test_output(true, iht_->contains(5), HT_Res<int>(FALSE_STATE, 0), "Contains 5");
      test_output(true, iht_->contains(4), HT_Res<int>(FALSE_STATE, 0), "Contains 4");
      ROME_INFO("All cases finished");
    }
    absl::Status stop_status = Stop();
    ROME_ASSERT_OK(stop_status);
    return absl::OkStatus();
  }

  // A function for communicating with the server that we are done. Will wait until server says it is ok to shut down
  absl::Status Stop() override {
    ROME_INFO("CLIENT :: Stopping client...");
    if (!master_client_){
      // if we aren't the master client, we need to arrive at the barrier
      if (barrier_ != nullptr) barrier_->arrive_and_wait();
    }

    // send the ack to let the server know that we are done
    tcp::EndpointManager* endpoint = tcp::EndpointManager::getInstance(endpoint_ctx_, host_.address.c_str());
    tcp::message send_buffer;
    endpoint->send_server(&send_buffer);
    ROME_INFO("CLIENT :: Sent Ack");

    // Wait to receive an ack back. Letting us know that the other clients are done.
    tcp::message recv_buffer;
    endpoint->recv_server(&recv_buffer);
    ROME_INFO("CLIENT :: Received Ack");

    // Return ok status
    return absl::OkStatus();
  }

private:
  Client(const MemoryPool::Peer &host, const tcp::EndpointContext ctx, ExperimentParams &params, std::barrier<> *barrier, IHT* iht, bool master_client)
      : host_(host), endpoint_ctx_(ctx), params_(params), barrier_(barrier), iht_(iht), master_client_(master_client) {
        if (params.unlimited_stream()) progression = 10000;
        else progression = params_.op_count() * 0.001;
      }

  int count = 0;

  const MemoryPool::Peer host_;
  const tcp::EndpointContext endpoint_ctx_;
  const ExperimentParams params_;
  std::barrier<> *barrier_;
  IHT* iht_;
  bool master_client_;

  int progression;
};