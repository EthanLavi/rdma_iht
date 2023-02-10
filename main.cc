#include <memory>

#include "iht_ds.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/connection_manager/connection_manager.h"

using ::rome::rdma::MemoryPool;
using ::rome::rdma::ConnectionManager;

constexpr char iphost[] = "10.10.1.1";
constexpr char ippeer[] = "10.10.1.2";
constexpr uint16_t portNum = 18001;

// typedef ConnectionManager<channel_type> cm_type;
// typedef RdmaChannel<TwoSidedRdmaMessenger<kMemoryPoolMessengerCapacity, kMemoryPoolMessageSize>, EmptyRdmaAccessor> channel_type;
using cm_type = MemoryPool::cm_type;

int main(){
    // ROME_INIT_LOG();
    MemoryPool::Peer host{0, std::string(iphost), portNum};
    MemoryPool::Peer receiver{1, std::string(ippeer), portNum};
    std::vector<MemoryPool::Peer> peers;
    peers.push_back(receiver);
    cm_type* t = new cm_type(0);
    std::unique_ptr<cm_type> ut = std::make_unique<cm_type>(0);
    RdmaIHT ds = RdmaIHT(host, ut);
    ds.Init(host, peers);
}