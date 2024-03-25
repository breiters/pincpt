#pragma once

#include "bucket.h"

#include <memory>
#include <unordered_map>
#include <vector>


class Region
{
public:
    Region(char *region);
    ~Region();
    void register_datastruct();
    void on_region_entry();
    void on_region_exit();
    void demangle_name();
    void print_csv(FILE *csv_out);

#if 0
    static std::unordered_map<char *, Region *> &regions()
    {
        static std::unordered_map<char *, Region *> s_regions{};
        return s_regions;
    }
#endif

private:
    char    *region_;
    unsigned thread_count_;

    std::vector<std::vector<BucketPair>> buckets_;
    std::vector<std::vector<BucketPair>> buckets_entry_;
};

extern std::unordered_map<char *, Region *> g_regions;
