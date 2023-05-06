#include <memory>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <iostream>
#include <ostream>
#include <thread>

// View c++ version __cplusplus
#define version_info __cplusplus

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "role_server.h"
#include "role_client.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/logging/logging.h"
#include "proto/experiment.pb.h"
#include "rome/util/proto_util.h"
#include "google/protobuf/text_format.h"

ABSL_FLAG(std::string, experiment_params, "", "Experimental parameters");
ABSL_FLAG(bool, send_bulk, false, "If to run bulk operations. (More for benchmarking)");
ABSL_FLAG(bool, send_test, false, "If to test the functionality of the methods.");
ABSL_FLAG(bool, send_exp, false, "If to run an experiment");

#define PATH_MAX 4096

using ::rome::rdma::MemoryPool;
using ::rome::rdma::ConnectionManager;

constexpr char iphost[] = "node0";
constexpr uint16_t portNum = 18000;

using cm_type = MemoryPool::cm_type;

int main(int argc, char** argv){
    ROME_INIT_LOG();
    absl::ParseCommandLine(argc, argv);
    bool bulk_operations = absl::GetFlag(FLAGS_send_bulk);
    bool test_operations = absl::GetFlag(FLAGS_send_test);
    bool do_exp = absl::GetFlag(FLAGS_send_exp);
    ExperimentParams params = ExperimentParams();
    std::string experiment_parms = absl::GetFlag(FLAGS_experiment_params);
    bool success = google::protobuf::TextFormat::MergeFromString(experiment_parms, &params);
    ROME_ASSERT(success, "Couldn't parse protobuf");

    // Get hostname to determine who we are
    char hostname[4096];
    gethostname(hostname, 4096);

    // Start initializing a vector of peers
    MemoryPool::Peer host{0, std::string(iphost), portNum};
    MemoryPool::Peer self;
    bool outside_exp = true;
    std::vector<MemoryPool::Peer> peers;
    peers.push_back(host);

    if (params.node_count() == 0){
        ROME_INFO("Cannot start experiment. Node count was found to be 0");
        exit(1);
    }
    
    // Set values if we are host machine as well
    if (hostname[4] == '0'){
        self = host;
        outside_exp = false;
    }

    // Make the peer list by iterating through the node count
    for(int n = 1; n < params.node_count(); n++){
        // Create the ip_peer (really just node name)
        std::string ippeer = "node";
        std::string node_id = std::to_string(n);
        ippeer.append(node_id);
        // Create the peer and add it to the list
        MemoryPool::Peer next{static_cast<uint16_t>(n), ippeer, portNum};
        peers.push_back(next);
        // Compare after 4th character to node_id
        if (strncmp(hostname + 4, node_id.c_str(), node_id.length()) == 0){
            // If matching, next is self
            self = next;
            outside_exp = false;
        }
    }

    if (outside_exp){
        ROME_INFO("Not in experiment. Shutting down");
        return 0;
    }

    // Make a memory pool for the node to share among all client instances
    uint32_t block_size = 1 << params.region_size();
    MemoryPool pool = MemoryPool(self, std::make_unique<MemoryPool::cm_type>(self.id));
    
    absl::Status status = pool.Init(block_size, peers);
    ROME_ASSERT_OK(status);
    ROME_INFO("Created memory pool");

    std::vector<std::thread> threads;
    if (hostname[4] == '0'){
        // If dedicated server-node, we must start the server
        threads.emplace_back(std::thread([&](){
            // We are the server
            std::unique_ptr<Server> server = Server::Create(host, peers, params);
            bool done = false;
            ROME_INFO("Server Created");
            absl::Status run_status = server->Launch(&pool, &done, params.runtime());            
            ROME_DEBUG("Running the server works? {}", run_status.ok());
        }));
        if (!do_exp){
            // Just do server when we are running testing operations
            threads[0].join();
            exit(0);
        }
    }

    if (!do_exp){
        // Not doing experiment, so just create some test clients
        std::unique_ptr<Client> client = Client::Create(&pool, self, host, peers, params, nullptr);
        if (bulk_operations){
            absl::Status status = client->Operations(true);
            ROME_DEBUG("Starting client is ok? {}", status.ok());
        } else if (test_operations) {
            absl::Status status = client->Operations(false);
            ROME_DEBUG("Starting client is ok? {}", status.ok());
        }
        exit(0);
    }

    // Barrier to start all the clients at the same time
    std::barrier client_sync = std::barrier(params.thread_count());

    for(int n = 0; n < params.thread_count(); n++){
        // Add the thread
        threads.emplace_back(std::thread([&](){
            // Create and run a client in a thread
            std::unique_ptr<Client> client = Client::Create(&pool, self, host, peers, params, &client_sync);
            bool done = false;
            ROME_INFO("Client Created");
            absl::Status run_status = Client::Run(std::move(client), &done);
            ROME_INFO("Running the IHT works? {}", run_status.ok());
        }));
    }

    // Join all threads
    int i = 0;
    for (auto it = threads.begin(); it != threads.end(); it++){
        ROME_INFO("Syncing {}", ++i);
        auto t = it;
        t->join();
    }
    return 0;
}
