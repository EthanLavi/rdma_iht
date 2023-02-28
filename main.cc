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
        std::cout << "Contains 5: " << ds.contains(5) << std::endl;
        std::cout << "Contains 4: " << ds.contains(4) << std::endl;
        std::cout << "Insert 5: " << ds.insert(5) << std::endl;
        std::cout << "Contains 5: " << ds.contains(5) << std::endl;
        std::cout << "Contains 4: " << ds.contains(4) << std::endl;
        std::cout << "Remove 5" << ds.remove(5) << std::endl;
        std::cout << "Contains 5: " << ds.contains(5) << std::endl;
        std::cout << "Contains 4: " << ds.contains(4) << std::endl;
    }

    /*if (am_host){
        for(int i = 0; i < 1000; i++){
            ds.insert(i);
        }
        std::cout << "Finished insert operations" << std::endl;
    } else {
        int j;
        for(j = 1000; j > 0; j--){
            if (ds.contains(j)){
                std::cout << j << std::endl;
                break;
            }
        }
        if (j == 0){
            std::cout << "Contains error" << std::endl;
        }
    }*/ /* Advanced Test Case */
    return 0;
}
