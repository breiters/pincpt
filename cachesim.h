#pragma once

#include "bucket.h"
#include "memoryblock.h"
#include <list>
#include <unordered_map>
#include <vector>

class CacheSim
{
public:
    CacheSim() : buckets_{std::vector<Bucket>{Bucket::min_dists.size()}} {}

    // StackIterator on_block_new(const MemoryBlock &mb);
    StackIterator on_block_new(MemoryBlock &&mb);
    int           on_block_seen(StackIterator &it);

    void incr_access(uint bucket) { buckets_[bucket].access_count++; }
    // count access with infinite reuse distance
    void incr_access_inf() { incr_access(buckets_.size() - 1); }

    StackIterator stack_begin() { return stack_.begin(); }

    inline const std::vector<Bucket> &buckets() const { return buckets_; }

    void print_stack()
    {
#if (RD_DEBUG && (RD_VERBOSE > 1))
        eprintf("\nstack:\n");
        int           m      = 1;
        StackIterator marker = buckets_[m].marker;
        for (auto it = stack_.begin(); it != stack_.end(); it++) {
            it->print();
            if (marker == it) {
                eprintf("^~~~ Marker\n");
                marker = buckets_[++m].marker;
            }
            if (it->bucket > RD_PRINT_MARKER_MAX)
                break;
        }
        eprintf("\n");
#endif
    }

private:
    uint next_bucket_{1u}; //

    std::list<MemoryBlock> stack_;   // Stack structure with MemoryBlock as element
    std::vector<Bucket>    buckets_; //

    void move_markers(uint);
    void on_next_bucket_gets_active();
    void check_consistency();
};

class PartitionedCache
{
public:
    CacheSim partition0_{};
    CacheSim partition1_{};

    size_t              cs_num_;    // idx in cachesims vector
    std::vector<size_t> ds_nums_{}; // all datastructs included in partition 0

    void add_datastruct(size_t ds_num);
    bool contains(size_t ds_num) const;

    void incr_access(int bucket, size_t ds_num)
    {
        if (contains(ds_num)) {
            partition0_.incr_access(bucket);
        } else {
            partition1_.incr_access(bucket);
        }
    }

    // increment cold misses (infinite reuse distance)
    void incr_access_inf(size_t ds_num)
    {
        int bucket_inf = Bucket::min_dists.size() - 1;
        incr_access(bucket_inf, ds_num);
    }

    // StackIterator on_block_new(IteratorContainer &ic);
    StackIterator on_block_new(size_t ds_num);
    int           on_block_seen(size_t ds_num, StackIterator &it);

    void print_csv(FILE *csv_out, const char *region) const;
    void print_csv(FILE *csv_out, const char *region, const std::vector<BucketPair> &buckets) const;
};

extern std::vector<PartitionedCache *> g_cachesims; // LRU stack objects

#if 0
class Cache
{
public:
    using AddrMap = std::unordered_map<Addr, IteratorContainer, std::hash<S>>;
    static AddrMap &addr_2_iterator_map() {
        static AddrMap map;
        return map;
    }

private:
    std::vector<PartitionedCache *> caches_; // one LRU cache per data object
};

class MemoryHierarchy
{
    Cache &private_cache(int tid) {
        return private_caches_[tid];
    }

    Cache &shared_cache(int tid) {
        return shared_caches_[shared_cache_index(tid)];
    }

private:
    int shared_cache_index(int tid) {
        return 0; // TODO
    }

    std::vector<Cache> private_caches_;
    std::vector<Cache> shared_caches_;
};
#endif
