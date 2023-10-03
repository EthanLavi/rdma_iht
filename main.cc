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
#include "context_manager.h"
#include "tcp.h"

ABSL_FLAG(std::string, experiment_params, "", "Experimental parameters");
ABSL_FLAG(bool, send_bulk, false, "If to run bulk operations. (More for benchmarking)");
ABSL_FLAG(bool, send_test, false, "If to test the functionality of the methods.");
ABSL_FLAG(bool, send_exp, false, "If to run an experiment");

#define PATH_MAX 4096

using ::rome::rdma::MemoryPool;
using ::rome::rdma::ConnectionManager;

constexpr uint16_t portNum = 18000;

using cm_type = MemoryPool::cm_type;

// The optimial number of memory pools is mp=min(t, MAX_QP/n) where n is the number of nodes and t is the number of threads
// To distribute mp (memory pools) across t threads, it is best for t/mp to be a whole number
// TODO: Reimplement test cases!

void exiting() {
    ROME_INFO("Exiting");
    tcp::SocketManager* socket_handle = tcp::SocketManager::getInstance(true);
    if (socket_handle != NULL){
        socket_handle->releaseResources();
    }
    tcp::EndpointManager::releaseResources();
}

int main(int argc, char** argv){
    ROME_INIT_LOG();
    std::atexit(exiting);
    absl::ParseCommandLine(argc, argv);
    bool bulk_operations = absl::GetFlag(FLAGS_send_bulk);
    bool test_operations = absl::GetFlag(FLAGS_send_test);
    bool do_exp = absl::GetFlag(FLAGS_send_exp);
    assert(!bulk_operations && !test_operations && do_exp); // TODO: remove
    ExperimentParams params = ExperimentParams();
    ResultProto result_proto = ResultProto();
    std::string experiment_parms = absl::GetFlag(FLAGS_experiment_params);
    bool success = google::protobuf::TextFormat::MergeFromString(experiment_parms, &params);
    ROME_ASSERT(success, "Couldn't parse protobuf");

    // Check node count
    if (params.node_count() <= 0 || params.thread_count() <= 0){
        ROME_INFO("Cannot start experiment. Node/thread count was found to be 0");
        exit(1);
    }
    // Check we are in this experiment
    if (params.node_id() >= params.node_count()){
        ROME_INFO("Not in this experiment. Exiting");
        exit(0);
    }

    // Get hostname to determine who we are
    char hostname[4096];
    gethostname(hostname, 4096);

    // Determine number of memory pools
    int mp = std::min(params.thread_count(), (int) std::floor(params.qp_max() / params.node_count()));
    if (mp == 0) mp = 1; // Make sure if node_count > qp_max, we don't end up with 0 memory pools
    
    ROME_INFO("Distributing {} MemoryPools across {} threads", mp, params.thread_count());

    // Start initializing a vector of peers
    volatile bool done = false; // (Should be atomic?)
    std::vector<MemoryPool::Peer> peers;
    for(uint16_t n = 0; n < mp * params.node_count(); n++){
        // Create the ip_peer (really just node name)
        std::string ippeer = "node";
        std::string node_id = std::to_string((int) floor(n / mp));
        ippeer.append(node_id);
        // Create the peer and add it to the list
        MemoryPool::Peer next{n, ippeer, static_cast<uint16_t>(portNum + n)};
        peers.push_back(next);
    }
    // Check peer list
    for(int i = 0; i < peers.size(); i++){
        ROME_INFO("Peer list {}:{}@{}", i, peers.at(i).id, peers.at(i).address);
    }
    MemoryPool::Peer host = peers.at(0);
    // Initialize memory pools into an array
    std::vector<std::thread> mempool_threads;
    MemoryPool* pools[mp];
    ContextManger manager = ContextManger([&](){
        for(int i = 0; i < mp; i++){
            delete pools[i];
        }
    });

    // Create multiple memory pools to be shared (have to use threads since Init is blocking)
    uint32_t block_size = 1 << params.region_size();
    for(int i = 0; i < mp; i++){
        mempool_threads.emplace_back(std::thread([&](int mp_index, int self_index){
            MemoryPool::Peer self = peers.at(self_index);
            MemoryPool* pool = new MemoryPool(self, std::make_unique<MemoryPool::cm_type>(self.id), mp != params.thread_count());
            absl::Status status_pool = pool->Init(block_size, peers);
            ROME_ASSERT_OK(status_pool);
            pools[mp_index] = pool;
        }, i, (params.node_id() * mp) + i));
    }
    // Let the init finish
    for(int i = 0; i < mp; i++){
        mempool_threads[i].join();
    }

    // Create a list of client and server  threads
    std::vector<std::thread> threads;
    if (hostname[4] == '0'){
        // If dedicated server-node, we must start the server
        threads.emplace_back(std::thread([&](){
            // Initialize X connections
            tcp::SocketManager* manager = tcp::SocketManager::getInstance();
            for(int i = 0; i < params.thread_count() * params.node_count(); i++){
                // TODO: Can we have a per-node connection? For now its prob ok
                manager->accept_conn();
            }
            // We are the server
            ROME_INFO("Server Created");
            absl::Status run_status = Server::Launch(&done, params.runtime(), [&](){
                // iht.try_rehash();
                // TODO: Allow for rehashing this way? Remove?
            });
            ROME_ASSERT_OK(run_status);
            for(int i = 0; i < mp; i++){
                pools[i]->KillWorkerThread();
            }
            ROME_INFO("[SERVER THREAD] -- End of execution; -- ");
        }));
    }

    // Initialize T endpoints, one for each thread
    tcp::EndpointContext endpoint_contexts[params.thread_count()];
    for(uint16_t i = 0; i < params.thread_count(); i++){
        endpoint_contexts[i] = tcp::EndpointContext(i);
        tcp::EndpointManager* manager = tcp::EndpointManager::getInstance(endpoint_contexts[i], host.address.c_str());
        assert(manager->is_init(endpoint_contexts[i]));
    }
    
    std::barrier client_sync = std::barrier(params.thread_count());
    WorkloadDriverProto results[params.thread_count()];
    for(int i = 0; i < params.thread_count(); i++){
        threads.emplace_back(std::thread([&](int thread_index){
            int mempool_index = thread_index % mp;
            MemoryPool* pool = pools[mempool_index];
            MemoryPool::Peer self = peers.at((params.node_id() * mp) + mempool_index);
            tcp::EndpointContext ctx = endpoint_contexts[thread_index];
            IHT iht = IHT(self, pool);
            if (self.id == host.id){
                // If we are the host
                remote_ptr<anon_ptr> root_ptr = iht.InitAsFirst();
                tcp::ExchangePointer(ctx, self, host, root_ptr);
            } else {
                remote_ptr<anon_ptr> root_ptr = tcp::ExchangePointer(ctx, self, host, remote_nullptr);
                iht.InitFromPointer(root_ptr);
            }
            ROME_INFO("Creating client");
            // Create and run a client in a thread
            std::unique_ptr<Client> client = Client::Create(host, ctx, params, &client_sync, &iht, thread_index == 0);
            absl::StatusOr<WorkloadDriverProto> output = Client::Run(std::move(client), &done, 0.5 / (double) (params.node_count() * params.thread_count()));
            if (output.ok()){
                results[thread_index] = output.value();
            } else {
                ROME_ERROR("Client run failed");
            }
            ROME_INFO("[CLIENT THREAD] -- End of execution; -- ");
        }, i));
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
