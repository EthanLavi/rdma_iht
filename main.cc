#include <memory>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <iostream>
#include <ostream>

#include "iht_ds.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/logging/logging.h"

#define PATH_MAX 4096

using ::rome::rdma::MemoryPool;
using ::rome::rdma::ConnectionManager;

constexpr char iphost[] = "10.10.1.1";
constexpr char ippeer[] = "10.10.1.2";

constexpr uint16_t portNum = 18000;

using cm_type = MemoryPool::cm_type;

void test_output(int actual, int expected, std::string message){
    if (actual != expected){
        std::cout << message << " func(): " << actual << " expected=" << expected << std::endl;
        exit(1);
    }
}

int main(){
    ROME_INIT_LOG();

    MemoryPool::Peer host{0, std::string(iphost), portNum};
    MemoryPool::Peer receiver{1, std::string(ippeer), portNum + 1};
    MemoryPool::Peer self;
    std::vector<MemoryPool::Peer> peers;

    char hostname[4096];
    gethostname(hostname, 4096);

    bool am_host = false;

    if (hostname[4] != '0'){
        // we are not node0, we are a peer
        self = receiver;
        peers.push_back(host);
        am_host = false;
    } else {
        // we are node0, the host
        self = host;
        peers.push_back(receiver);
        am_host = true;
    }

    struct config confs{8, 128};

    RdmaIHT ds = RdmaIHT(self, std::make_unique<cm_type>(self.id), confs);
    ds.Init(host, peers);

    if (am_host){
        std::cout << "Starting Test Cases..." << std::endl;
        test_output(ds.contains(5), 0, "Contains 5");
        test_output(ds.contains(4), 0, "Contains 4");
        test_output(ds.insert(5), 1, "Insert 5");
        
        test_output(ds.contains(5), 1, "Contains 5");
        test_output(ds.contains(4), 0, "Contains 4");

        test_output(ds.remove(5), 1, "Remove 5");
        test_output(ds.contains(5), 0, "Contains 5");
        test_output(ds.contains(4), 0, "Contains 4");

        for(int i = 10; i < 10000; i++){
            // ds.insert(i);
        }

        // test_output(ds.contains(1000), 1, "Contains 1000");
        // test_output(ds.contains(10), 1, "Contains 10");

        std::cout << "All cases passed" << std::endl;
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
