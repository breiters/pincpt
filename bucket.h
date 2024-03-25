#pragma once

#include "memoryblock.h"

#include <limits>
#include <vector>

class Bucket
{
public:
    using min_type   = unsigned;
    using count_type = unsigned long;

    count_type    access_count = count_type{0};
    StackIterator marker;

    static std::vector<min_type> min_dists;              // minimum reuse distance in bucket
    static constexpr min_type    inf_dist{~min_type{0}}; // std::numeric_limits<min_type>::max();

    // static_assert(inf_dist == std::numeric_limits<min_type>::max());

    inline void add_sub(const Bucket &add, const Bucket &sub) { access_count += add.access_count - sub.access_count; }
};

using BucketPair = std::pair<Bucket, Bucket>;
