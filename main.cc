#include <memory>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <iostream>
#include <ostream>

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

int main(){
    ROME_INIT_LOG();

    MemoryPool::Peer host{0, std::string(iphost), portNum};
    MemoryPool::Peer receiver{1, std::string(ippeer), portNum + 1};
    std::vector<MemoryPool::Peer> peers;

    char hostname[4096];
    gethostname(hostname, 4096);

    bool am_host = false;

    struct config confs{8, 128};

    if (hostname[4] != '0'){
        // we are not node0, we are a peer
        peers.push_back(host);
        am_host = false;
        std::unique_ptr<Client> client = Client::Create(receiver, host, peers);
        // bool done = false;
        absl::Status init_status = client->Start(); // start the client. Don't use when using Run
        ROME_DEBUG("Init client is ok? {}", init_status.ok());
        absl::Status status = client->Operations(); // client->Run(std::move(client), &done);
        ROME_DEBUG("Starting client is ok? {}", status.ok());
    } else {
        // we are node0, the host
        peers.push_back(receiver);
        am_host = true;
        std::unique_ptr<Server> server = Server::Create(host, peers, confs);
        absl::Status status = server->LaunchTestLoopback();
        ROME_INFO("Starting server is ok? {}", status.ok());
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
