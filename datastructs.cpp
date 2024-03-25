#include "datastructs.h"
#include "bucket.h"
#include "cachesim.h"
#include "region.h"

#include "config.h"

#include <cassert>
#include <vector>

std::vector<Datastruct> Datastruct::datastructs;

[[maybe_unused]] void Datastruct::combine(size_t ds_idx)
{
    // eprintf("%s\n", __PRETTY_FUNCTION__);
    if (datastructs[ds_idx].nbytes < RD_COMBINE_THRESHOLD) {
        return;
    }

    // for (size_t ds_idx_other = 0; ds_idx_other < Datastruct::datastructs.size() - 1; ds_idx_other++)
    /** TODO: do we combine with "no datastruct" data object? --> yes! in complement ds */
    for (size_t ds_idx_other = 1; ds_idx_other < ds_idx; ds_idx_other++) {

        if (Datastruct::datastructs[ds_idx_other].nbytes < RD_COMBINE_THRESHOLD) {
            continue;
        }

        g_cachesims.push_back(new PartitionedCache{});
        // size_t cs_idx = g_cachesims.size() - 1;

        // indices_of[ds_idx].push_back(cs_idx);
        // indices_of[ds_idx_other].push_back(cs_idx);

        g_cachesims.back()->add_datastruct(ds_idx);
        g_cachesims.back()->add_datastruct(ds_idx_other);

        for (auto &key_value : g_regions) {
            key_value.second->register_datastruct();
        }
        // printf("combining ds=%zu with ds=%zu\n", ds_idx, ds_idx_other);
    }
}

void Datastruct::register_datastruct(Datastruct &info)
{
    Datastruct::datastructs.push_back(info);
    size_t ds_idx = Datastruct::datastructs.size() - 1;

    // g_cachesims.push_back(new CacheSim{0});
    g_cachesims.push_back(new PartitionedCache{});
    if(ds_idx > RD_NO_DATASTRUCT) {
        g_cachesims.back()->add_datastruct(ds_idx);
    }

    for (auto &key_value : g_regions) {
        key_value.second->register_datastruct();
    }
#if RD_COMBINED_DATASTRUCTS
    combine(ds_idx);
#endif /* RD_COMBINED_DATASTRUCTS */
}

#if 0
    for (size_t i = 0; i < datastructs.size(); i++) {
        printf("ds_idx %lu contained in: \n", i);
        for (auto cs : indices_of[i]) {
            printf("%d\n", cs);
        }
    }
}
#endif

/** TODO: maybe use better algorithm to get datastruct (sort datastructs by address) **/
size_t Datastruct::datastruct_num(Addr addr)
{
    size_t i = 0;
    for (auto &ds : Datastruct::datastructs) {
        if (!ds.is_freed && (uint64_t)addr >= (uint64_t)ds.address && (uint64_t)addr < (uint64_t)ds.address + (uint64_t)ds.nbytes) {
            return i;
        }
        i++;
    }
    return RD_NO_DATASTRUCT;
}
