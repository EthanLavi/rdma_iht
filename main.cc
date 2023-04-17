#include <memory>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <iostream>
#include <ostream>
#include <thread>         

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/status/status.h"
#include "role_server.h"
#include "role_client.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/logging/logging.h"

#define PATH_MAX 4096

using ::rome::rdma::MemoryPool;
using ::rome::rdma::ConnectionManager;

constexpr char iphost[] = "node0";
constexpr char ippeer[] = "node1";
constexpr uint16_t portNum = 18000;

using cm_type = MemoryPool::cm_type;

ABSL_FLAG(bool, send_bulk, false, "If to run bulk operations. (More for benchmarking)");
ABSL_FLAG(bool, send_test, false, "If to test the functionality of the methods.");

int main(int argc, char** argv){
    ROME_INIT_LOG();
    absl::ParseCommandLine(argc, argv);
    bool bulk_operations = absl::GetFlag(FLAGS_send_bulk);
    bool test_operations = absl::GetFlag(FLAGS_send_test);

    MemoryPool::Peer host{0, std::string(iphost), portNum};
    // MemoryPool::Peer rec{0, std::string(iphost), portNum+1};
    MemoryPool::Peer receiver{1, std::string(ippeer), portNum + 1};
    std::vector<MemoryPool::Peer> peers;

    char hostname[4096];
    gethostname(hostname, 4096);
    bool am_host = false;

    /*
    if (hostname[4] != '0'){
        ROME_DEBUG("Not in this experiment shutting down");
        return 0;
    }

    std::thread t1([&](){
        ROME_DEBUG("Entered the server area");
        // we are the server
        peers.push_back(rec);
        std::unique_ptr<Server> server = Server::Create(host, peers);
        bool done = false;
        absl::Status run_status = server->Launch(&done, 0);            
        ROME_DEBUG("Running the server works? {}", run_status.ok());
    });
    std::thread t2([&](){
        ROME_DEBUG("Entered the client area");
        // we are the client
        peers.push_back(host);
        std::unique_ptr<Client> client = Client::Create(rec, host, peers);
        absl::Status run_status = client->Operations();
        ROME_DEBUG("Running client works? {}", run_status.ok());
    });
    
    t1.join();
    t2.join();
    */
    
    if (hostname[4] != '0'){
        // we are not node0, we are a peer
        peers.push_back(host);
        am_host = false;
        std::unique_ptr<Client> client = Client::Create(receiver, host, peers);
        if (bulk_operations){
            bool done = false;
            absl::Status run_status = Client::Run(std::move(client), &done);
            ROME_DEBUG("Running the IHT works? {}", run_status.ok());
        } else if (test_operations) {
            absl::Status init_status = client->Start();
            ROME_DEBUG("Init client is ok? {}", init_status.ok());
            absl::Status status = client->Operations();
            ROME_DEBUG("Starting client is ok? {}", status.ok());
            absl::Status stop_status = client->Stop();
            ROME_DEBUG("Stopping client is ok? {}", stop_status.ok());
        }
    } else {
        // we are node0, the host
        peers.push_back(receiver);
        am_host = true;
        std::unique_ptr<Server> server = Server::Create(host, peers);
        bool done = false;
        absl::Status status = server->Launch(&done, 0);
        ROME_DEBUG("Starting server is ok? {}", status.ok());
    }

    /*std::cout << "Any output?" << std::endl;

    if (am_host){
        for(int i = 0; i < 1000; i++){
            ds.insert(i);
        }
        std::cout << "Finished insert operations" << std::endl;
    } else {
        int j;
        for(j = 1000; j > 0; j--){
            std::cout << j << "J-test:" << ds.contains(j) << std::endl;
        }
        std::cout << "J-error testing: " << j << std::endl;
    } */ /* Advanced Test Case */
    return 0;
}
