#include "region.h"
#include "bucket.h"
#include "cachesim.h"
#include "datastructs.h"
#include "pin.H"
#include <cassert>
#include <cstring>
#include <cxxabi.h>
#include <iostream>

std::unordered_map<char *, Region *> g_regions;

Region::Region(char *region) : region_{strdup(region)}, thread_count_{0}
{
    // add region buckets for every CacheSim, init with zero
    size_t num_cachesims = g_cachesims.size();

    buckets_.reserve(num_cachesims);
    buckets_entry_.reserve(num_cachesims);

    for (size_t i = 0; i < num_cachesims; i++) {
        buckets_.push_back(std::vector<BucketPair>{Bucket::min_dists.size()});
        buckets_entry_.push_back(std::vector<BucketPair>{Bucket::min_dists.size()});
    }
}

Region::~Region()
{
    // std::cout << "free region: " << region_ << "\n";
    free(region_);
    region_ = nullptr;
}

void Region::on_region_entry()
{
    // make snapshot of current buckets access counts
    thread_count_++;
    if (thread_count_ > 1) {
        return;
    }
    // std::cout << "entry region: " << region_ << "\n";

    /** TODO: better implementation */
    size_t cs_num = 0;
    for (auto &cs : g_cachesims) {
        size_t b = 0;
        for (auto &bucket : cs->partition0_.buckets()) {
            buckets_entry_[cs_num][b].first = bucket;
            b++;
        }
        b = 0;
        for (auto &bucket : cs->partition1_.buckets()) {
            buckets_entry_[cs_num][b].second = bucket;
            b++;
        }
        cs_num++;
    }
}

void Region::on_region_exit()
{
    // last thread leaving a region does the counting
    thread_count_--;
    if (thread_count_ > 0) {
        return;
    }
    // std::cout << "exit region: " << region_ << "\n";

    /** TODO: better implementation */
    size_t cs_num = 0;
    for (auto &cs : g_cachesims) {
        size_t b = 0;
        for (auto &bucket : cs->partition0_.buckets()) {
            buckets_[cs_num][b].first.add_sub(bucket, buckets_entry_[cs_num][b].first);
            b++;
        }
        b = 0;
        for (auto &bucket : cs->partition1_.buckets()) {
            buckets_[cs_num][b].second.add_sub(bucket, buckets_entry_[cs_num][b].second);
            b++;
        }
        cs_num++;
    }
}

void Region::register_datastruct()
{
    buckets_.push_back(std::vector<BucketPair>{Bucket::min_dists.size()});
    buckets_entry_.push_back(std::vector<BucketPair>{Bucket::min_dists.size()});
}

/** TODO: there is also a more portable pin function for this **/
void Region::demangle_name()
{
    int   status         = -1;
    char *demangled_name = abi::__cxa_demangle(region_, NULL, NULL, &status);
    // printf("Demangled: %s\n", demangled_name);

    // set region name to the demangled name without arguments if it was a mangled function
    if (nullptr != demangled_name) {
        free(region_);
        region_ = demangled_name;
        strtok(region_, "(");
    }
    // std::cout << "new region: " << region_ << "\n";
}

void Region::print_csv(FILE *csv_out)
{
    demangle_name();
    size_t cs_num = 0;
    for (auto &cs : g_cachesims) {
        cs->print_csv(csv_out, region_, buckets_[cs_num]);
        cs_num++;
    }
}
