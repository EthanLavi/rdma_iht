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
    std::vector<MemoryPool::Peer> peers;
    peers.push_back(host);

    if (params.node_count() == 0){
        ROME_INFO("Cannot start experiment. Node count was found to be 0");
        exit(1);
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
        }
    }
    
    for(int i = 0; i < params.node_count(); i++){
        ROME_INFO("-- {}", peers[i].id);
    }
    ROME_INFO("self {}", self.id);
    ROME_INFO("host {}", host.id);

    // Temp guard
    exit(0);

    std::vector<std::thread*> threads(0);
    if (hostname[4] == '0'){
        // If dedicated server-node, we must start the server
        std::thread t = std::thread([&](){
            // We are the server
            std::unique_ptr<Server> server = Server::Create(host, peers, params);
            bool done = false;
            absl::Status run_status = server->Launch(&done, params.runtime());            
            ROME_DEBUG("Running the server works? {}", run_status.ok());
        });
        threads.push_back(&t);
        if (!do_exp){
            // Just do server when we are running testing operations
            t.join();
            exit(0);
        }
    }

    if (!do_exp){
        // Not doing experiment, so just create some test clients
        std::unique_ptr<Client> client = Client::Create(self, host, peers, params, nullptr);
        if (bulk_operations){
            absl::Status status = client->Operations(true);
            ROME_DEBUG("Starting client is ok? {}", status.ok());
        } else if (test_operations) {
            absl::Status status = client->Operations(false);
            ROME_DEBUG("Starting client is ok? {}", status.ok());
        }
        exit(0);
    }

    std::barrier client_sync = std::barrier(params.thread_count());

    for(int n = 0; n < params.thread_count(); n++){
        std::thread t = std::thread([&](){
            std::unique_ptr<Client> client = Client::Create(self, host, peers, params, &client_sync);
            bool done = false;
            absl::Status run_status = Client::Run(std::move(client), &done);
            ROME_DEBUG("Running the IHT works? {}", run_status.ok());
        });
        threads.push_back(&t);
    }

    // Join all threads
    for (auto it = threads.begin(); it != threads.end(); it++){
        auto t = *it;
        t->join();
    }
    return 0;
}
