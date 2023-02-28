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


struct config {
    size_t elist_size;
    size_t plist_size;
};

#define ELIST_SIZE 8
#define PLIST_SIZE 128

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
        /*struct pair_t {
            int val;
        };*/

        size_t count = 0; // The number of live elements in the Elist
        int pairs[ELIST_SIZE]; // A list of pairs to store (stored as remote pointer to start of the contigous memory block)
        // Insert into elist a int
        void elist_insert(const int &key){
            pairs[count] = key;
            ++count;
        }
    };

    // PointerList stores EList pointers and assorted locks
    struct PList : Base {
        // A pointer lock pair
        struct pair_t {
            remote_baseptr base; // Pointer to base, the super class of Elist or Plist
            std::atomic<lock_state_t> lock; // A lock to represent if the base is open or not
        };

        pair_t buckets[PLIST_SIZE]; // Pointer lock pairs

        PList(){
            for (size_t i = 0; i < PLIST_SIZE; ++i){
                buckets[i].lock = E_UNLOCKED;
                buckets[i].base = remote_nullptr;
            }
        }
    };

    typedef remote_ptr<PList> remote_plist;    
    typedef remote_ptr<EList> remote_elist;

    const size_t elist_size; // Size of all elists // TODO: Replace variable use with define for now
    const size_t plist_size; // Size of all plists
    remote_ptr<PList> root;  // Start of plist
    std::hash<int> pre_hash; // Hash function from k -> size_t [this currently does nothing :: though included for templating this class]
    
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
        // TODO: the plist size should double in size
        remote_plist new_p = pool_.Allocate<PList>();

        // hash everything from the full elist into it
        remote_elist source = static_cast<remote_elist>(parent->buckets[pidx].base);
        for (size_t i = 0; i < source->count; i++){
            uint64_t b = level_hash(source->pairs[i], pdepth + 1) % pcount;
            if (new_p->buckets[b].base == remote_nullptr){
                remote_elist e = pool_.Allocate<EList>();
                new_p->buckets[b].base = static_cast<remote_baseptr>(e);
            }
            remote_elist dest = static_cast<remote_elist>(new_p->buckets[b].base);
            dest->elist_insert(source->pairs[i]);
        }

        pool_.Deallocate(source);
        return new_p;
    }
public:
    using conn_type = MemoryPool::conn_type;

    RdmaIHT(MemoryPool::Peer self, std::unique_ptr<MemoryPool::cm_type> cm, struct config confs);

    void Init(MemoryPool::Peer host, const std::vector<MemoryPool::Peer> &peers);

    bool contains(int value){
        // start at root
        remote_plist curr = root;
        size_t depth = 1, count = plist_size;
        while (true) {
            uint64_t bucket = level_hash(value, depth) % count;
            if (!acquire(curr->buckets[bucket].lock)){
                // Can't lock then we are at a sub-plist
                curr = static_cast<remote_plist>(curr->buckets[bucket].base);
                depth++;
                count *= 2;
                continue;
            }

            // Past this point we have recursed to an elist
            if (curr->buckets[bucket].base == remote_nullptr){
                // empty elist
                curr->buckets[bucket].lock = E_UNLOCKED;
                return false;
            }

            // Get elist and linear search
            remote_elist e = static_cast<remote_elist>(curr->buckets[bucket].base);
            for (size_t i = 0; i < e->count; i++){
                // Linear search to determine if elist already contains the value
                if (e->pairs[i] == value){
                    curr->buckets[bucket].lock = E_UNLOCKED;
                    return true;
                }
            }

            // Can't find, unlock and return fasle
            curr->buckets[bucket].lock = E_UNLOCKED;
            return false;
        }
    }
    
    bool insert(int value){
        // start at root
        remote_plist curr = root;
        size_t depth = 1, count = plist_size;
        while (true){
            uint64_t bucket = level_hash(value, depth) % count;
            if (!acquire(curr->buckets[bucket].lock)){
                // Can't lock then we are at a sub-plist
                curr = static_cast<remote_plist>(curr->buckets[bucket].base);
                depth++;
                count *= 2;
                continue;
            }

            // Past this point we have recursed to an elist
            if (curr->buckets[bucket].base == remote_nullptr){
                // empty elist
                remote_elist e = pool_.Allocate<EList>();
                e->elist_insert(value);
                remote_baseptr e_base = static_cast<remote_baseptr>(e);
                curr->buckets[bucket].base = e_base;
                curr->buckets[bucket].lock = E_UNLOCKED;
                // successful insert
                return true;
            }

            remote_elist e = static_cast<remote_elist>(curr->buckets[bucket].base);
            for (size_t i = 0; i < e->count; i++){
                // Linear search to determine if elist already contains the value
                if (e->pairs[i] == value){
                    curr->buckets[bucket].lock = E_UNLOCKED;
                    return false;
                }
            }

            if (e->count < elist_size) {
                // Check for enough insertion room -- insert, unlock, return
                e->elist_insert(value);
                curr->buckets[bucket].lock = E_UNLOCKED;
                return true;
            }

            // Need more room so rehash into plist and perma-unlock
            remote_plist p = rehash(curr, count, depth, bucket);
            curr->buckets[bucket].base = static_cast<remote_baseptr>(p);
            curr->buckets[bucket].lock = P_UNLOCKED;
        }
    }
    
    bool remove(int value){
        // start at root
        remote_plist curr = root;
        size_t depth = 1, count = plist_size;
        while (true) {
            uint64_t bucket = level_hash(value, depth) % count;
            if (!acquire(curr->buckets[bucket].lock)){
                // Can't lock then we are at a sub-plist
                curr = static_cast<remote_plist>(curr->buckets[bucket].base);
                depth++;
                count *= 2;
                continue;
            }

            // Past this point we have recursed to an elist
            if (curr->buckets[bucket].base == remote_nullptr){
                // empty elist
                curr->buckets[bucket].lock = E_UNLOCKED;
                return false;
            }

            // Get elist and linear search
            remote_elist e = static_cast<remote_elist>(curr->buckets[bucket].base);
            for (size_t i = 0; i < e->count; i++){
                // Linear search to determine if elist already contains the value
                if (e->pairs[i] == value){
                    if (e->count > 1){
                        // Edge swap if not count=0|1
                        e->pairs[i] = e->pairs[e->count - 1];
                    }
                    e->count -= 1;
                    curr->buckets[bucket].lock = E_UNLOCKED;
                    return true;
                }
            }

            // Can't find, unlock and return fasle
            curr->buckets[bucket].lock = E_UNLOCKED;
            return false;
        }
    }
};
