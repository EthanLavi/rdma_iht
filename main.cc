#include <memory>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <fstream>
#include <iostream>
#include <ostream>
#include <thread>
#include <cstdlib>
#include <cmath>
#include <algorithm>

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
#include "protos/experiment.pb.h"
#include "rome/util/proto_util.h"
#include "google/protobuf/text_format.h"
#include "common.h"
#include "exchange_ptr.h"

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

// The optimial number of memory pools is mp=min(t, MAX_QP/n) where n is the number of nodes and t is the number of threads
// To distribute mp (memory pools) across t threads, it is best for t/mp to be a whole number

void exiting() {
    ROME_INFO("Exiting");
    tcp::SocketManager* socket_handle = tcp::SocketManager::getInstance(true);
    if (socket_handle != NULL){
        socket_handle->releaseResources();
    }
    tcp::EndpointManager* manager = tcp::EndpointManager::getInstance(NULL);
    if (manager != NULL){
        manager->releaseResources();
    }
}

int main(int argc, char** argv){
    ROME_INIT_LOG();
    std::atexit(exiting);
    absl::ParseCommandLine(argc, argv);
    bool bulk_operations = absl::GetFlag(FLAGS_send_bulk);
    bool test_operations = absl::GetFlag(FLAGS_send_test);
    bool do_exp = absl::GetFlag(FLAGS_send_exp);
    ExperimentParams params = ExperimentParams();
    ResultProto result_proto = ResultProto();
    std::string experiment_parms = absl::GetFlag(FLAGS_experiment_params);
    bool success = google::protobuf::TextFormat::MergeFromString(experiment_parms, &params);
    ROME_ASSERT(success, "Couldn't parse protobuf");
    
    // Get hostname to determine who we are
    char hostname[4096];
    gethostname(hostname, 4096);

    // Determine number of memory pools
    int mp = std::min(params.thread_count(), (int) std::floor(params.qp_max() / params.node_count()));
    ROME_INFO("Distributing {} MemoryPools across {} threads", mp, params.thread_count());

    // Start initializing a vector of peers
    volatile bool done = false;
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
    for(int n = 1; n < params.node_count() && do_exp; n++){
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
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // So we get this printed last
        ROME_INFO("Not in experiment. Shutting down");
        return 0;
    }

    // Make a memory pool for the node to share among all client instances
    uint32_t block_size = 1 << params.region_size();
    MemoryPool pool = MemoryPool(self, std::make_unique<MemoryPool::cm_type>(self.id));

    for(int i = 0; i < peers.size(); i++){
        ROME_INFO("Peer list {}:{}@{}", i, peers.at(i).id, peers.at(i).address);
    }
    
    absl::Status status_pool = pool.Init(block_size, peers);
    ROME_ASSERT_OK(status_pool);
    ROME_INFO("Created memory pool");

    IHT iht = IHT(self, &pool);
    if (self.id == host.id){
        remote_ptr<anon_ptr> root_ptr = iht.InitAsFirst();
        tcp::ExchangePointer(self, host, peers, root_ptr);
    } else {
        remote_ptr<anon_ptr> root_ptr = tcp::ExchangePointer(self, host, peers, remote_nullptr);
        iht.InitFromPointer(root_ptr);
    }
    ROME_INFO("Created an IHT");

    std::vector<std::thread> threads;
    if (hostname[4] == '0'){
        // If dedicated server-node, we must start the server
        threads.emplace_back(std::thread([&](){
            // We are the server
            std::unique_ptr<Server> server = Server::Create(host, peers, params, &pool);
            ROME_INFO("Server Created");
            absl::Status run_status = server->Launch(&done, params.runtime(), [&iht](){
                iht.try_rehash();
            });
            ROME_ASSERT_OK(run_status);
            pool.KillWorkerThread();
            ROME_INFO("[SERVER THREAD] -- End of execution; -- ");
        }));
    }

    if (!do_exp){
        // Not doing experiment, so just create some test clients
        std::unique_ptr<Client> client = Client::Create(self, host, peers, params, nullptr, &iht, true);
        if (bulk_operations){
            absl::Status status = client->Operations(true);
            ROME_ASSERT_OK(status);
        } else if (test_operations) {
            absl::Status status = client->Operations(false);
            ROME_ASSERT_OK(status);
        }
        // Wait for server to complete
        done = true;
        threads[0].join();
        ROME_INFO("[TEST] -- End of execution; -- ");
        exit(0);
    }

    // Barrier to start all the clients at the same time
    std::barrier client_sync = std::barrier(params.thread_count());
    WorkloadDriverProto results[params.thread_count()];
    for(int n = 0; n < params.thread_count(); n++){
        // Add the thread
        threads.emplace_back(std::thread([&](int index){
            ROME_INFO("Created a client");
            // Create and run a client in a thread
            std::unique_ptr<Client> client = Client::Create(self, host, peers, params, &client_sync, &iht, index == 0);
            absl::StatusOr<WorkloadDriverProto> output = Client::Run(std::move(client), &done, 0.5 / (double) params.node_count());
            if (output.ok()){
                results[index] = output.value();
            } else {
                ROME_ERROR("Client run failed");
            }
            ROME_INFO("[CLIENT THREAD] -- End of execution; -- ");
        }, n));
    }

    // Join all threads
    int i = 0;
    for (auto it = threads.begin(); it != threads.end(); it++){
        ROME_INFO("Syncing {}", ++i);
        auto t = it;
        t->join();
    }

    *result_proto.mutable_params() = params;

    auto total_ops = 0;
    for (int i = 0; i < params.thread_count(); i++){
        IHTWorkloadDriverProto* r = result_proto.add_driver();
        std::string output;
        auto ops = results[i].ops().counter().count();
        total_ops += ops;
        ROME_INFO("{}:{}", i, ops);
        results[i].SerializeToString(&output);
        r->MergeFromString(output);
    }
    
    ROME_INFO("Total Ops: {}", total_ops);
    ROME_INFO("Compiled Proto Results ### {}", result_proto.DebugString());

    std::ofstream filestream("iht_result.pbtxt");
    filestream << result_proto.DebugString();
    filestream.flush();
    filestream.close();

    ROME_INFO("[EXPERIMENT] -- End of execution; -- ");
    exit(0);
}
