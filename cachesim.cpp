#include "cachesim.h"
#include "bucket.h"
#include "datastructs.h"
#include "region.h"

#include "config.h"

#include "pin.H"

#include <algorithm>
#include <cassert>
#include <utility>

/**
 * can NOT use std::vector<CacheSim> here because PIN C++ stl invalidates the list iterators of existing
 * elements when pushing to a vector
 */
std::vector<PartitionedCache *> g_cachesims; // LRU stack objects

void CacheSim::on_next_bucket_gets_active()
{
    // eprintf("\n%s\n", __func__);

    // set new buckets marker to end of stack first then set marker to last stack element
    buckets_[next_bucket_].marker = stack_.end();

    --(buckets_[next_bucket_].marker);

    eprintf("marker now on:");
    buckets_[next_bucket_].marker->print();

#if RD_DEBUG
    StackIterator it = stack_.begin();
    for (unsigned i = 0; i < Bucket::min_dists[next_bucket_]; i++)
        it++;
    assert(it == buckets_[next_bucket_].marker);
#endif /* RD_DEBUG */

    // last stack element is now in the next higher bucket
    (buckets_[next_bucket_].marker)->bucket++;

    assert((buckets_[next_bucket_].marker)->bucket == next_bucket_);

    next_bucket_++;

#if RD_DEBUG
    check_consistency();
#endif /* RD_DEBUG */
    print_stack();
}

/**
 * @brief Adds new memory block to top of stack. Moves active bucket markers.
 * Adds next bucket if necessary.
 *
 * @param mb The to memory block.
 * @return The stack begin iterator
 */
StackIterator CacheSim::on_block_new(MemoryBlock &&mb)
{
    eprintf("\n%s\n", __func__);
    stack_.push_front(std::move(mb));

    print_stack();

    // move markers upwards after inserting new block on stack
    move_markers(next_bucket_ - 1);

    // does another bucket get active?
    if (Bucket::min_dists[next_bucket_] != Bucket::inf_dist && (stack_.size() > Bucket::min_dists[next_bucket_])) {
        on_next_bucket_gets_active();
    }

#if RD_DEBUG > 1
    check_consistency();
#endif /* RD_DEBUG */
    eprintf("\n%s\n", __func__);

    return stack_.begin();
}

/**
 * @brief
 *
 * @param blockIt
 */
int CacheSim::on_block_seen(StackIterator &blockIt)
{
    // eprintf("\n%s\n", __func__);

    // if already on top of stack: do nothing (bucket is zero anyway)
    if (blockIt == stack_.begin()) {
        return 0;
    }

    eprintf("block was not on top... moving block to top of stack\n");
    blockIt->print();

    // move all markers below current memory blocks bucket
    int bucket = blockIt->bucket;
    move_markers(bucket);

    // put current memory block on top of stack
    stack_.splice(stack_.begin(), stack_, blockIt);

    eprintf("\nstack after splice:");
    print_stack();

    // bucket of blockIt is zero now because it is on top of stack
    blockIt->bucket = 0;

#if RD_DEBUG > 1
    check_consistency();
#endif /* RD_DEBUG */

    return bucket;
}

/**
 * Sanity check:
 * - every active marker must be found in stack in the right order
 * - distance of bucket marker to stack begin must be equal to the min distance for the bucket
 */
void CacheSim::check_consistency()
{
#if RD_DEBUG
    const size_t  DO_CHECK = 10;
    static size_t iter     = 0;
    iter++;
    if (iter < DO_CHECK) {
        return;
    }
    iter          = 0;
    auto it_start = stack_.begin();
    for (uint b = 1; b < next_bucket_; b++) {
        unsigned distance = 0;
        for (auto it = it_start; it != buckets_[b].marker; it++) {
            assert(it != stack_.end());
            distance++;
        }
        assert(distance == Bucket::min_dists[b]);
    }
#endif /* RD_DEBUG */
}

void CacheSim::move_markers(uint topBucket)
{
    assert(topBucket < next_bucket_ && topBucket >= 0);

    for (uint b = 1; b <= topBucket; b++) {
        assert(buckets_[next_bucket_].marker != stack_.begin());

        // decrement marker so it stays always on same distance to stack begin
        --(buckets_[b].marker);

        // increment bucket of memory block where current marker points to
        (buckets_[b].marker)->bucket++;
    }
}

void PartitionedCache::add_datastruct(size_t ds_num) { ds_nums_.push_back(ds_num); }

bool PartitionedCache::contains(size_t ds_num) const
{
    return std::find(ds_nums_.begin(), ds_nums_.end(), ds_num) != ds_nums_.end();
}

StackIterator PartitionedCache::on_block_new(size_t ds_num)
{
    if (contains(ds_num)) {
        return partition0_.on_block_new(MemoryBlock{}); // TODO: FORWARD
    } else {
        return partition1_.on_block_new(MemoryBlock{});
    }
}

int PartitionedCache::on_block_seen(size_t ds_num, StackIterator &it)
{
    if (contains(ds_num)) {
        return partition0_.on_block_seen(it);
    } else {
        return partition1_.on_block_seen(it);
    }
}

/**********************************************
 *     CSV output functions
 **********************************************/

#define CSV_FORMAT "%d,%s,%s,%p,%zu,%d,%s,%u,%u,%lu,%lu\n"

extern unsigned g_max_threads;

/** TODO: FIX DUPLICATED CODE */
void PartitionedCache::print_csv(FILE *csv_out, const char *region) const
{
    Datastruct ds{};
    size_t     MAX_LEN = 1024u;
    char       ds_name_buf[MAX_LEN];

    // global address space accesses
    if (ds_nums_.size() == 0) {
        ds.address   = (void *)0x0;
        ds.nbytes    = 0U;
        ds.line      = 0;
        ds.file_name = "main file";
        snprintf(ds_name_buf, MAX_LEN, "%zu", RD_NO_DATASTRUCT);
    }
    // single datastruct
    else if (ds_nums_.size() == 1) {
        ds = Datastruct::datastructs[ds_nums_[0]];
        snprintf(ds_name_buf, MAX_LEN, "%zu", ds_nums_[0]);
    }
    // combined datastruct
    else {
        ds_name_buf[0] = '[';
        ds_name_buf[1] = ' ';
        size_t offset  = 2;
        size_t nbytes  = 0;
        for (auto num : ds_nums_) {
            offset += snprintf(ds_name_buf + offset, MAX_LEN - offset, "%zu | ", num);
            nbytes += Datastruct::datastructs[num].nbytes;
        }
        ds_name_buf[offset - 2] = ']';
        ds_name_buf[offset - 1] = '\0';

        ds.address   = (void *)0x0;
        ds.nbytes    = 0U;
        ds.line      = 0;
        ds.file_name = " ";
    }

    for (size_t b = 0; b < partition0_.buckets().size(); b++) {
        fprintf(csv_out,
                CSV_FORMAT,
                MEMBLOCKLEN,
                region,
                ds_name_buf,
                ds.address,
                ds.nbytes,
                ds.line,
                ds.file_name.c_str(),
                Bucket::min_dists[b],
                g_max_threads,
                partition0_.buckets()[b].access_count,
                partition1_.buckets()[b].access_count);
    }
}

void PartitionedCache::print_csv(FILE *csv_out, const char *region, const std::vector<BucketPair> &buckets) const
{
    Datastruct ds{};
    size_t     MAX_LEN = 1024u;
    char       ds_name_buf[MAX_LEN];

    // global address space accesses
    if (ds_nums_.size() == 0) {
        ds.address   = (void *)0x0;
        ds.nbytes    = 0U;
        ds.line      = 0;
        ds.file_name = "main file";
        snprintf(ds_name_buf, MAX_LEN, "%zu", RD_NO_DATASTRUCT);
    }
    // single datastruct
    else if (ds_nums_.size() == 1) {
        ds = Datastruct::datastructs[ds_nums_[0]];
        snprintf(ds_name_buf, MAX_LEN, "%zu", ds_nums_[0]);
    }
    // combined datastruct
    else {
        ds_name_buf[0] = '[';
        ds_name_buf[1] = ' ';
        size_t offset  = 2;
        size_t nbytes  = 0;
        for (auto num : ds_nums_) {
            offset += snprintf(ds_name_buf + offset, MAX_LEN - offset, "%zu | ", num);
            nbytes += Datastruct::datastructs[num].nbytes;
        }
        ds_name_buf[offset - 2] = ']';
        ds_name_buf[offset - 1] = '\0';

        ds.address   = (void *)0x0;
        ds.nbytes    = 0U;
        ds.line      = 0;
        ds.file_name = " ";
    }

    for (size_t b = 0; b < buckets.size(); b++) {
        fprintf(csv_out,
                CSV_FORMAT,
                MEMBLOCKLEN,
                region,
                ds_name_buf,
                ds.address,
                ds.nbytes,
                ds.line,
                ds.file_name.c_str(),
                Bucket::min_dists[b],
                g_max_threads,
                buckets[b].first.access_count,
                buckets[b].second.access_count);
    }
}
