#pragma once

typedef void* Addr;

// cache line size on the machine used for profiling
#ifndef CACHE_LINESIZE
#    define CACHE_LINESIZE 64
#endif

#define RD_HISTOGRAM   1
#define RD_MIN_BUCKETS 1

// cache line size of the target architecture (must be a power-of-two)
#define MEMBLOCKLEN 256
#define MEMBLOCK_MASK ~(MEMBLOCKLEN - 1)

// Consistency checks?
#define DEBUG        0

// 2: Huge amount of debug output, 1: checks, 0: silent
#define VERBOSE      0

// uses INS_IsStackRead/Write: misleading with -fomit-frame-pointer
#define IGNORE_STACK 1

// Assertions and consistency check?
// #define RD_DEBUG 1

#if defined(NDEBUG)
    #define RD_DEBUG 0
#else
    #define RD_DEBUG 1
#endif

// 2: Huge amount of debug output, 1: checks, 0: silent
#define RD_VERBOSE 0

// Print only up to N-th marker in debug output
#define RD_PRINT_MARKER_MAX 2

// Do not account for zero distance accesses?
#define RD_DO_NOT_COUNT_ZERO_DISTANCE 1

// Simulate isolated datastructs?
#define RD_DATASTRUCT_THRESHOLD 100
// #define RD_DATASTRUCT_THRESHOLD 0

// Also profile for "combined" isolated datastructs?
#define RD_COMBINED_DATASTRUCTS 1
// #define RD_COMBINE_THRESHOLD 50000
#define RD_COMBINE_THRESHOLD 100
//#define RD_COMBINE_THRESHOLD 0

#define MAX_THREADS 12

#if RD_VERBOSE
#define eprintf(...) fprintf (stderr, __VA_ARGS__)
#else
#define eprintf(...)
#endif
