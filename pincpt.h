#pragma once

#include "config.h"

#include <unordered_map>

#include <cstdint>

// make sure that memblocks are powers of two
static constexpr bool is_pow2(int a) { return !(a & (a - 1)); }
static_assert(is_pow2(MEMBLOCKLEN), "is_pow2(MEMBLOCKLEN)");

// find first bit set
static constexpr int ffs_constexpr(int x)
{
    int n = 0;
    while ((x & 1) == 0) {
        x >>= 1;
        n++;
    }
    return n;
}
static_assert(ffs_constexpr(256) == 8, "ffs_constexpr(256) == 8"); // sanity check
static constexpr int first_set = ffs_constexpr(MEMBLOCKLEN);

struct S {
}; // dummy class to provide custom hash object
namespace std
{
    template <>
    struct hash<S> {
        size_t operator()(Addr const &s) const noexcept
        {
            // last n bits of Addr s are all zero
            // => avoid collisions having keys that are all multiples of MEMBLOCKLEN
            return hash<Addr>{}((Addr)(((uintptr_t)s) >> first_set));
        }
    };
} // namespace std

struct alignas(CACHE_LINESIZE) AddrWrapper {
    Addr addr = 0;
};
static_assert(sizeof(AddrWrapper) == CACHE_LINESIZE, "sizeof(AddrWrapper) != CACHE_LINESIZE");
