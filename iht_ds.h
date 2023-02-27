#include <infiniband/verbs.h>
#include <cstdint>
#include <atomic>

#include "rome/rdma/channel/sync_accessor.h"
#include "rome/rdma/connection_manager/connection.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/rdma_memory.h"

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;


struct Integer {
    int value;
};

class RdmaIHT {
private:
    bool is_host_;
    MemoryPool::Peer self_;
    MemoryPool pool_;

    // Enum to represent the state of a node. P-lists cannot be locked 
    enum lock_state_t { E_LOCKED, E_UNLOCKED, P_UNLOCKED };

    // "Super class" for the elist and plist structs
    struct Base {};
    typedef remote_ptr<Base> remote_baseptr;

    // ElementList stores a bunch of K/V pairs. IHT employs a "seperate chaining"-like approach. 
    // Rather than storing via a linked list (with easy append), it uses a fixed size array
    struct EList : Base {
        typedef remote_ptr<EList> remote_elist;

        size_t count; // The number of live elements in the Elist
        int pairs[]; // A list of integers to store
        private:
            // Factory for EList construction
            EList(){}
        public:
            // EList factory method
            static remote_elist make(size_t size){
                remote_elist e = pool_.Allocate<EList>();
                e->count = 0;
                return e;
            }

            // Insert into elist a int
            void elist_insert(const int &key){
                pairs[count] = key;
                ++count;
            }
    };

    // PointerList stores EList pointers and assorted locks
    struct PList : Base {
        typedef remote_ptr<PList> remote_plist;
        // A pointer lock pair
        struct pair_t {
            remote_baseptr base; // Pointer to base, the super class of Elist or Plist
            std::atomic<lock_state_t> lock; // A lock to represent if the base is open or not
        };

        pair_t buckets[]; // Pointer lock pairs
    private:
        PList(){}
    public:
        static remote_plist make(size_t size){
            remote_plist p = pool_.Allocate<PList>();
            for (size_t i = 0; i < size; ++i){
                p->buckets[i].lock = E_UNLOCKED;
                p->buckets[i].base = remote_nullptr;
            }
            return p;
        }
    };

    typedef remote_ptr<PList> remote_plist;    
    typedef remote_ptr<EList> remote_elist;

    const size_t elist_size; // Size of all elists
    const size_t plist_size; // Size of all plists
    remote_ptr<PList> root;  // Start of plist
    std::hash<int> pre_hash; // Hash function from k -> size_t
    
    // TODO: shouldn't this be a remote pointer?
    static bool acquire(std::atomic<lock_state_t> &lock){
        // Spin while trying to acquire the lock
        while (true){
            lock_state_t v = lock.load();

            // If we can switch from unlock to lock status
            if (v == E_UNLOCKED && lock.compare_exchange_weak(v, E_LOCKED)){
                return true;
            }

            // Permanent unlock
            if (v == P_UNLOCKED){
                return false;
            }
        }
    }

    // Hashing function to decide bucket size
    uint64_t level_hash(const int &key, size_t level){
        return level ^ pre_hash(key);
    }

    /// Rehash function
    /// @param parent The P-List whose bucket needs rehashing
    /// @param pcount The number of elements in `parent`
    /// @param pdepth The depth of `parent`
    /// @param pidx   The index in `parent` of the bucket to rehash
    remote_plist rehash(remote_plist parent, size_t pcount, size_t pdepth, size_t pidx){
        // double in size
        remote_plist new_p = PList::make(pcount * 2);

        // hash everything from the full elist into it
        remote_elist source = static_cast<remote_elist>(parent->buckets[pidx].base);
        for (size_t i = 0; i < source->count; i++){
            uint64_t b = level_hash(source->pairs[i], pdepth + 1) % pcount;
            if (new_p->buckets[b].base == remote_nullptr){
                new_p->buckets[b].base = static_cast<remote_baseptr>(EList::make(elist_size));
            }
            remote_elist dest = static_cast<remote_elist>(new_p->buckets[b].base);
            dest->elist_insert(source->pairs[i]);
        }

        pool_.Deallocate(source);
        return new_p;
    }
public:
    using conn_type = MemoryPool::conn_type;

    RdmaIHT(MemoryPool::Peer self, std::unique_ptr<MemoryPool::cm_type> cm);

    void Init(MemoryPool::Peer host, const std::vector<MemoryPool::Peer> &peers);

    bool contains(int value);
    bool insert(int value);
    bool remove(int value);
};
