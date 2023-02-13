#include <memory>
#include <unistd.h>
#include <stdio.h>

#include <string>

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

    if (hostname[4] != '0'){
        // we are not node0, we are a peer
        self = receiver;
        peers.push_back(host);
    } else {
        // we are node0, the host
        self = host;
        peers.push_back(receiver);
    }

    RdmaIHT ds = RdmaIHT(self, std::make_unique<cm_type>(self.id));
    ds.Init(host, peers);
    return 0;
}
