#pragma once

#include <infiniband/verbs.h>
#include <cstdint>
#include <atomic>

#include "rome/rdma/channel/sync_accessor.h"
#include "rome/rdma/connection_manager/connection.h"
#include "rome/rdma/connection_manager/connection_manager.h"
#include "rome/rdma/memory_pool/memory_pool.h"
#include "rome/rdma/rdma_memory.h"
#include "rome/logging/logging.h"

using ::rome::rdma::ConnectionManager;
using ::rome::rdma::MemoryPool;
using ::rome::rdma::remote_nullptr;
using ::rome::rdma::remote_ptr;
using ::rome::rdma::RemoteObjectProto;

// TODO: Make allocation of PList be dynamic and not static size

template<class K, class V, int ELIST_SIZE, int PLIST_SIZE>
class RdmaIHT {
private:
    bool is_host_;
    MemoryPool::Peer self_;

    // "Poor-mans" enum to represent the state of a node. P-lists cannot be locked 
    const uint64_t E_LOCKED = 0;
    const uint64_t E_UNLOCKED = 1;
    const uint64_t P_UNLOCKED = 2;

    // "Super class" for the elist and plist structs
    struct Base {};
    typedef std::atomic<uint64_t> lock_type;
    typedef remote_ptr<Base> remote_baseptr;
    typedef remote_ptr<lock_type> remote_lock;

    // ElementList stores a bunch of K/V pairs. IHT employs a "seperate chaining"-like approach.
    // Rather than storing via a linked list (with easy append), it uses a fixed size array
    struct EList : Base {
        struct pair_t {
            K key;
            V val;
        };

        size_t count = 0; // The number of live elements in the Elist
        pair_t pairs[ELIST_SIZE]; // A list of pairs to store (stored as remote pointer to start of the contigous memory block)
        
        // Insert into elist a deconstructed pair
        void elist_insert(const K key, const V val){
            pairs[count] = {key, val};
            count++;
        }

        // Insert into elist a pair
        void elist_insert(const pair_t pair){
            pairs[count] = pair;
            count++;
        }

        EList(){
            ROME_DEBUG("Running EList Constructor!");
        }
    };

    // A pointer lock pair
    struct plist_pair_t {
        remote_baseptr base; // Pointer to base, the super class of Elist or Plist
        remote_lock lock; // A lock to represent if the base is open or not
        // TODO: Maybe I can manipulate the lock without needing a pointer?
    };

    // PointerList stores EList pointers and assorted locks
    struct PList : Base {
        plist_pair_t buckets[PLIST_SIZE]; // Pointer lock pairs
    };

    typedef remote_ptr<PList> remote_plist;    
    typedef remote_ptr<EList> remote_elist;

    /// @brief Initialize the plist with values.
    /// @param p the plist pointer to init
    /// @param depth the depth of p, needed for PLIST_SIZE == base_size * (2 ** (depth - 1))
    /// pow(2, depth)
    void InitPList(remote_plist p, int mult_modder){
        for (size_t i = 0; i < PLIST_SIZE * mult_modder; i++){
            p->buckets[i].lock = pool_.Allocate<lock_type>();
            *p->buckets[i].lock = E_UNLOCKED;
            p->buckets[i].base = remote_nullptr;
        }
    }

    remote_ptr<PList> root;  // Start of plist
    std::hash<K> pre_hash; // Hash function from k -> size_t [this currently does nothing as the value of the int can just be returned :: though included for templating this class]
    
    /// Acquire a lock on the bucket. Will prevent others from modifying it
    bool acquire(remote_lock lock){
        // Spin while trying to acquire the lock
        while (true){
            remote_lock local_lock = lock;
            if (lock.id() != self_.id) local_lock = pool_.Read<lock_type>(lock);
            auto v = local_lock->load();
            if (lock.id() != self_.id) pool_.Deallocate<lock_type>(local_lock, 8); // free the local copy (have to delete "8" because we need to take into account alignment!)

            // Permanent unlock
            if (v == P_UNLOCKED){
                return false;
            }

            // If we can switch from unlock to lock status
            if (lock.id() != self_.id){
                if (pool_.CompareAndSwap<lock_type>(lock, E_UNLOCKED, E_LOCKED) == E_UNLOCKED) return true;
            } else if (v == E_UNLOCKED){
                if (lock->compare_exchange_weak(v, E_LOCKED)) return true;
            }
        }
    }

    /// @brief Unlock a lock. ==> the reverse of acquire
    /// @param lock the lock to unlock
    /// @param unlock_status what should the end lock status be.
    inline void unlock(remote_lock lock, uint64_t unlock_status){
        if (lock.id() != self_.id) pool_.AtomicSwap(lock, unlock_status);
        else *lock = unlock_status;
    }

    /// @brief Change the baseptr from a given bucket (could be remote as well) 
    /// @param before_localized_curr the start of the bucket list (plist)
    /// @param bucket the bucket to write to
    /// @param baseptr the new pointer that bucket should point to
    inline void change_bucket_pointer(remote_plist before_localized_curr, uint64_t bucket, remote_baseptr baseptr){
        ROME_INFO("Change bucket ptr");
        uint64_t address_of_baseptr = before_localized_curr.address();
        address_of_baseptr += sizeof(plist_pair_t) * bucket;
        remote_ptr<remote_baseptr> magic_baseptr = remote_ptr<remote_baseptr>(before_localized_curr.id(), address_of_baseptr);
        if (magic_baseptr.id() != self_.id) pool_.Write<remote_baseptr>(magic_baseptr, baseptr);
        else *magic_baseptr = baseptr;
    }

    // Hashing function to decide bucket size
    uint64_t level_hash(const K &key, size_t level){
        return level ^ pre_hash(key);
    }

    /// Rehash function
    /// @param parent The P-List whose bucket needs rehashing
    /// @param pcount The number of elements in `parent`
    /// @param pdepth The depth of `parent`
    /// @param pidx   The index in `parent` of the bucket to rehash
    remote_plist rehash(remote_plist parent, size_t pcount, size_t pdepth, size_t pidx){
        ROME_INFO("Depth of the data structure:: {}", pdepth);
        // TODO: the plist size should double in size
        pcount = pcount * 2;
        int plist_size_factor = (pcount / ELIST_SIZE); // pow(2, pdepth); // how much bigger than original size we are 
        
        // 2 ^ (depth) ==> in other words (depth:factor). 0:1, 1:2, 2:4, 3:8, 4:16, 5:32. 
        remote_plist new_p = pool_.Allocate<PList>(plist_size_factor);
        InitPList(new_p, plist_size_factor);

        // hash everything from the full elist into it
        remote_baseptr parent_bucket = parent->buckets[pidx].base;
        remote_elist source = static_cast<remote_elist>(parent_bucket.id() == self_.id ? parent_bucket : pool_.Read<Base>(parent_bucket));
        for (size_t i = 0; i < source->count; i++){
            uint64_t b = level_hash(source->pairs[i].key, pdepth + 1) % pcount;
            if (new_p->buckets[b].base == remote_nullptr){
                remote_elist e = pool_.Allocate<EList>();
                new_p->buckets[b].base = static_cast<remote_baseptr>(e);
            }
            remote_elist dest = static_cast<remote_elist>(new_p->buckets[b].base);
            dest->elist_insert(source->pairs[i]);
        }
        // Deallocate the old elist
        pool_.Deallocate(parent_bucket);
        return new_p;
    }
public:
    MemoryPool pool_;
    K result = 0; // value to modify upon completing an operation. Might as well grab the previous value if we are there. 

    using conn_type = MemoryPool::conn_type;

    RdmaIHT(MemoryPool::Peer self, std::unique_ptr<MemoryPool::cm_type> cm) : self_(self), pool_(self, std::move(cm)){};

    absl::Status Init(MemoryPool::Peer host, const std::vector<MemoryPool::Peer> &peers) {
        is_host_ = self_.id == host.id;
        uint32_t block_size = 1 << 16;

        absl::Status status = pool_.Init(block_size, peers);
        ROME_CHECK_OK(ROME_RETURN(status), status);

        if (is_host_){
            // Host machine, it is my responsibility to initiate configuration

            // Allocate data in pool
            RemoteObjectProto proto;
            remote_plist iht_root = pool_.Allocate<PList>();
            InitPList(iht_root, 1);    
            this->root = iht_root;
            proto.set_raddr(iht_root.address());

            // Iterate through peers
            for (auto p = peers.begin(); p != peers.end(); p++){
                // Form a connection with the machine
                auto conn_or = pool_.connection_manager()->GetConnection(p->id);
                ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);

                // Send the proto over
                status = conn_or.value()->channel()->Send(proto);
                ROME_CHECK_OK(ROME_RETURN(status), status);
            }
        } else {
            // Listen for a connection
            auto conn_or = pool_.connection_manager()->GetConnection(host.id);
            // ROME_CHECK_OK(ROME_RETURN(conn_or.status()), conn_or);

            // Try to get the data from the machine, repeatedly trying until successful
            auto got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
            while(got.status().code() == absl::StatusCode::kUnavailable) {
                got = conn_or.value()->channel()->TryDeliver<RemoteObjectProto>();
            }
            // ROME_CHECK_OK(ROME_RETURN(got.status()), got);

            // From there, decode the data into a value
            remote_plist iht_root = decltype(iht_root)(host.id, got->raddr());
            this->root = iht_root;
        }
        ROME_INFO("Init finished");

        return absl::OkStatus();
    }


    /// @brief Gets a value at the key.
    /// @param key the key to search on
    /// @return if the key was found or not. The value at the key is stored in RdmaIHT::result
    bool contains(K key){
        ROME_INFO("Starting contains...");
        // start at root
        remote_plist curr = pool_.Read<PList>(root);
        size_t depth = 1, count = PLIST_SIZE;
        bool oldBucketBase = root.id() != self_.id;
        while (true) {
            uint64_t bucket = level_hash(key, depth) % count;
            remote_baseptr bucket_base = curr->buckets[bucket].base;
            remote_baseptr base_ptr = bucket_base.id() == self_.id || bucket_base == remote_nullptr ? bucket_base : pool_.Read<Base>(bucket_base);
            if (!acquire(curr->buckets[bucket].lock)){
                ROME_INFO("Inside lock unexpectedly");
                // Can't lock then we are at a sub-plist
                if (oldBucketBase) pool_.Deallocate<PList>(curr); // deallocate if curr was not ours
                oldBucketBase = bucket_base.id() != self_.id; // setting the old bucket base
                curr = static_cast<remote_plist>(base_ptr);
                depth++;
                count *= 2; // TODO: Change back to 2 when we expand PList size
                continue;
            }

            // Past this point we have recursed to an elist
            if (base_ptr == remote_nullptr){
                // empty elist
                unlock(curr->buckets[bucket].lock, E_UNLOCKED);
                if (oldBucketBase) pool_.Deallocate<PList>(curr); // deallocate if curr was not ours
                return false;
            }

            // Get elist and linear search
            // Need to do a conditional read here because base_ptr might be ours. Basically we read the EList locally
            remote_elist e = static_cast<remote_elist>(base_ptr);
            for (size_t i = 0; i < e->count; i++){
                // Linear search to determine if elist already contains the key
                if (e->pairs[i].key == key){
                    unlock(curr->buckets[bucket].lock, E_UNLOCKED);
                    result = e->pairs[i].val;
                    if (bucket_base.id() != self_.id) pool_.Deallocate<EList>(e);
                    if (oldBucketBase) pool_.Deallocate<PList>(curr); // deallocate if curr was not ours
                    return true;
                }
            }

            // Can't find, unlock and return fasle
            unlock(curr->buckets[bucket].lock, E_UNLOCKED);
            if (bucket_base.id() != self_.id) pool_.Deallocate<EList>(e);
            if (oldBucketBase) pool_.Deallocate<PList>(curr); // deallocate if curr was not ours
            return false;
        }
    }
    
    /// @brief Insert a key and value into the iht. Result will become the value at the key if already present.
    /// @param key the key to insert
    /// @param value the value to associate with the key
    /// @return if the insert was successful
    bool insert(K key, V value){
        // start at root
        remote_plist curr = pool_.Read<PList>(root);
        remote_plist before_localized_curr = root;
        size_t depth = 1, count = PLIST_SIZE;
        while (true){
            uint64_t bucket = level_hash(key, depth) % count;
            remote_baseptr bucket_base = curr->buckets[bucket].base;
            remote_baseptr base_ptr = bucket_base.id() == self_.id || bucket_base == remote_nullptr ? bucket_base : pool_.Read<Base>(bucket_base);
            if (!acquire(curr->buckets[bucket].lock)){
                // Can't lock then we are at a sub-plist
                before_localized_curr = static_cast<remote_plist>(bucket_base);
                curr = static_cast<remote_plist>(base_ptr);
                depth++;
                count *= 2; // TODO: Change back to 2 when we expand PList size
                continue;
            }

            // Past this point we have recursed to an elist
            if (base_ptr == remote_nullptr){
                // empty elist
                remote_elist e = pool_.Allocate<EList>();
                e->elist_insert(key, value);
                remote_baseptr e_base = static_cast<remote_baseptr>(e);
                // modify the bucket's pointer
                change_bucket_pointer(before_localized_curr, bucket, e_base);
                unlock(curr->buckets[bucket].lock, E_UNLOCKED);
                // successful insert
                return true;
            }

            // We have recursed to an non-empty elist
            remote_elist e = static_cast<remote_elist>(base_ptr);
            for (size_t i = 0; i < e->count; i++){
                // Linear search to determine if elist already contains the key
                if (e->pairs[i].key == key){
                    result = e->pairs[i].val;
                    // Contains the key => unlock and return false
                    unlock(curr->buckets[bucket].lock, E_UNLOCKED);
                    return false;
                }
            }

            // Check for enough insertion room
            if (e->count < ELIST_SIZE) {
                // insert, unlock, return
                e->elist_insert(key, value);
                // If we are modifying the local copy, we need to write to the remote at the end
                change_bucket_pointer(before_localized_curr, bucket, static_cast<remote_baseptr>(e));
                // unlock and return true
                unlock(curr->buckets[bucket].lock, E_UNLOCKED);
                return true;
            }

            // Ignore this branch for now since we haven't implemented rehash
            // Need more room so rehash into plist and perma-unlock
            remote_plist p = rehash(curr, count, depth, bucket);
            // modify the bucket's pointer
            change_bucket_pointer(before_localized_curr, bucket, static_cast<remote_baseptr>(p));
            // keep local curr updated with remote curr
            curr->buckets[bucket].base = static_cast<remote_baseptr>(p);
            // unlock bucket
            unlock(curr->buckets[bucket].lock, P_UNLOCKED);
        }
    }
    
    /// @brief Will remove a value at the key. Will stored the previous value in result.
    /// @param key the key to remove at
    /// @return if the remove was successful
    bool remove(K key){
        // start at root
        remote_plist curr = pool_.Read<PList>(root);
        remote_plist before_localized_curr = root;
        size_t depth = 1, count = PLIST_SIZE;
        bool oldBucketBase = root.id() != self_.id;
        while (true) {
            uint64_t bucket = level_hash(key, depth) % count;
            remote_baseptr bucket_base = curr->buckets[bucket].base;
            remote_baseptr base_ptr = bucket_base.id() == self_.id || bucket_base == remote_nullptr ? bucket_base : pool_.Read<Base>(bucket_base);
            if (!acquire(curr->buckets[bucket].lock)){
                // Can't lock then we are at a sub-plist
                // Might have to do another read here. For now its fine because we aren't rehashing
                if (oldBucketBase) pool_.Deallocate<PList>(curr); // deallocate if curr was not ours
                oldBucketBase = bucket_base.id() != self_.id; // setting the old bucket base
                before_localized_curr = static_cast<remote_plist>(bucket_base);
                curr = static_cast<remote_plist>(base_ptr);
                depth++;
                count *= 2; // TODO: Change back to 2 when we expand PList size
                continue;
            }

            // Past this point we have recursed to an elist
            if (base_ptr == remote_nullptr){
                // empty elist, can just unlock and return false
                unlock(curr->buckets[bucket].lock, E_UNLOCKED);
                if (oldBucketBase) pool_.Deallocate<PList>(curr); // deallocate if curr was not ours
                return false;
            }

            // Get elist and linear search
            // Need to do a conditional read here because base_ptr might be ours. Basically we read the EList locally
            remote_elist e = static_cast<remote_elist>(base_ptr);
            for (size_t i = 0; i < e->count; i++){
                // Linear search to determine if elist already contains the value
                if (e->pairs[i].key == key){
                    result = e->pairs[i].val; // saving the previous value at key
                    if (e->count > 1){
                        // Edge swap if not count=0|1
                        e->pairs[i] = e->pairs[e->count - 1];
                    }
                    e->count -= 1;
                    // If we are modifying the local copy, we need to write to the remote at the end...
                    change_bucket_pointer(before_localized_curr, bucket, static_cast<remote_baseptr>(e));
                    // Unlock and return
                    unlock(curr->buckets[bucket].lock, E_UNLOCKED);
                    if (bucket_base.id() != self_.id) pool_.Deallocate<EList>(e);
                    if (oldBucketBase) pool_.Deallocate<PList>(curr); // deallocate if curr was not ours
                    return true;
                }
            }

            // Can't find, unlock and return false
            unlock(curr->buckets[bucket].lock, E_UNLOCKED);
            if (bucket_base.id() != self_.id) pool_.Deallocate<EList>(e);
            if (oldBucketBase) pool_.Deallocate<PList>(curr); // deallocate if curr was not ours
            return false;
        }
    }

    /// @brief Populate the data structure with values within the key range
    /// @param n the number of values to populate with
    /// @param keys the key range?
    /// @param values 
    void populate(int n, K* keys, V* values){
        for (int i = 0; i < n; i++)
            this->insert(keys[i], values[i]);
    }
};
