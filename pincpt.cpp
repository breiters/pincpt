/** On Linux IA-32 architectures, Pintools are built non-PIC (Position Independent Code),
 * which allows the compiler to inline both local and global functions. Tools for Linux
 * Intel(R) 64 architectures are built PIC, but the compiler will not inline any globally
 * visible function due to function pre-emption. Therefore, it is advisable to declare the
 * subroutines called by the analysis function as 'static' on Linux Intel(R) 64
 * architectures. **/

#include "pincpt.h"

#include "bucket.h"
#include "cachesim.h"
#include "config.h"
#include "datastructs.h"
#include "imgload.h"
#include "mcslock.h"
#include "memoryblock.h"
#include "region.h"

#include "pin.H"

#include <algorithm>
#include <list>
#include <unordered_map>
#include <vector>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unistd.h>

using std::list;
using std::string;
using std::unordered_map;
using std::vector;

// static unsigned long abefore_cnt = 0;

// each memory block can be contained in multiple stacks
// so we need to store the iterators to the memory block within each stack
//
// use hash map with custom hash function:
using AddrMap = std::unordered_map<Addr, IteratorContainer, std::hash<S>>;
AddrMap g_addrMap;

extern unsigned g_max_threads;
extern string   g_application_name;

static unsigned  num_threads = 0;
static PIN_MUTEX thread_mutex; // add / remove threads
static MCSLock   mcslock{};

bool        g_pindist_do_profile{true};
PIN_RWMUTEX g_rwlock; // required for new datastruct and region entry/exit events (see
                      // imgload.cpp)
unsigned g_max_threads{0};

static void on_block_new(Addr addr)
{
    IteratorContainer &ic = g_addrMap[addr];
    ic.ds_num_            = Datastruct::datastruct_num(addr);
    ic.addr_              = addr;

    for (PartitionedCache *pc : g_cachesims) {
        // ic.iterators.emplace_back(pc->on_block_new(ic.ds_num)); // TODO: move ?
        ic.iterators_.push_back(pc->on_block_new(ic.ds_num_)); // TODO: move ?
        pc->incr_access_inf(ic.ds_num_);
    }
}

[[maybe_unused]] static void on_block_prev(Addr addr)
{
    size_t ds_num = g_addrMap[addr].ds_num_;

    for (PartitionedCache *pc : g_cachesims) {
        pc->incr_access(0, ds_num);
    }
}

static void on_block_seen(AddrMap::iterator &it)
{
    IteratorContainer &ic     = it->second;
    size_t             ds_num = ic.ds_num_;

    if (Datastruct::datastructs[ds_num].is_freed) {
        ic.ds_num_ = Datastruct::datastruct_num(ic.addr_);
    }

    size_t idx = 0;

    for (PartitionedCache *pc : g_cachesims) {
        if (idx < ic.iterators_.size()) {
            int bucket = pc->on_block_seen(ds_num, ic.iterators_[idx]);
            pc->incr_access(bucket, ds_num);
        } else {
            // ic.iterators.emplace_back(pc->on_block_new(ic.ds_num)); // TODO: move ?
            ic.iterators_.push_back(pc->on_block_new(ic.ds_num_)); // TODO: move ?
            pc->incr_access_inf(ic.ds_num_);
        }
        idx++;
    }
}

static void RD_accessBlock(Addr addr)
{
    static AddrWrapper addr_prev[MAX_THREADS] = {0};
    bool               prev                   = (addr == addr_prev[PIN_ThreadId()].addr);

    if (prev) {
        // abefore_cnt++;

#if !RD_DO_NOT_COUNT_ZERO_DISTANCE
        on_block_prev(addr);
#endif
        // assert(g_cachesims[0]->stack_begin() == g_addrMap.find(addr)->second[0]);
    } else {
        auto it = g_addrMap.find(addr);

        if (it == g_addrMap.end()) {
            on_block_new(addr);
        } else {
            on_block_seen(it);
        }
    }

    addr_prev[PIN_ThreadId()].addr = addr;
}

static void free_memory()
{
    for (auto &region : g_regions) {
        delete region.second;
        region.second = nullptr;
    }

    for (auto &cs_ptr : g_cachesims) {
        delete cs_ptr;
        cs_ptr = nullptr;
    }
}

void RD_print_csv()
{
    const char *csv_header = "cachelinesize,region,data_object,addr,nbytes,line,file_"
                             "name,min_dist,threads,partition0,partition1\n";

    // generate filename
    constexpr size_t FILENAME_SIZE = 256u;
    char             csv_filename[FILENAME_SIZE];
    snprintf(csv_filename,
             FILENAME_SIZE,
             "pincpt-%s-%04ut.csv",
             g_application_name.c_str(),
             g_max_threads);

    FILE *csv_out = fopen(csv_filename, "w");
    fprintf(csv_out, "%s", csv_header);

    for (auto &cs : g_cachesims) {
        cs->print_csv(csv_out, "main");
    }

    for (const auto &region : g_regions) {
        region.second->print_csv(csv_out);
    }
    fclose(csv_out);
    free_memory(); // TODO: move somewhere else
}

void RD_init()
{
    g_addrMap.clear();
    // global cache simulation
    // g_cachesims.push_back(new CacheSim{});
    Datastruct ds{};
    Datastruct::register_datastruct(ds);
}

/* ===================================================================== */
/* Command line options                                                  */
/* ===================================================================== */

KNOB<int>
    KnobMinDist(KNOB_MODE_WRITEONCE, "pintool", "m", "4096", "minimum bucket distance");
KNOB<int> KnobDoubleSteps(
    KNOB_MODE_WRITEONCE, "pintool", "s", "1", "number of buckets for doubling distance");
KNOB<bool>
    KnobPIDPrefix(KNOB_MODE_WRITEONCE, "pintool", "p", "0", "prepend output by --PID--");

/* ===================================================================== */
/* Direct Callbacks                                                      */
/* ===================================================================== */

// size: #bytes accessed
static void memAccess(ADDRINT addr, UINT32 size)
{
    // printf("########## %p\n", (void*)addr);
    // bytes accessed: [addr, ... , addr+size-1]

    // calculate memory block (cacheline) of low address
    Addr a1 = (void *)(addr & MEMBLOCK_MASK);
    // calculate memory block (cacheline) of high address
    Addr a2 = (void *)((addr + size - 1) & MEMBLOCK_MASK);

#if RD_DEBUG
    static int threads;
#endif

    mcslock.lock(PIN_ThreadId());

    PIN_RWMutexReadLock(&g_rwlock);

#if RD_DEBUG
    threads++;
    assert(threads == 1);
#endif

    // single memory block accessed
    if (a1 == a2) {
        if (VERBOSE > 1)
            fprintf(stderr, " => %p\n", a1);
        RD_accessBlock(a1);
    }
    // memory access spans across two memory blocks
    // => two memory blocks accessed
    else {
        if (VERBOSE > 1)
            fprintf(stderr, " => CROSS %p/%p\n", a1, a2);
        RD_accessBlock(a1);
        RD_accessBlock(a2);
    }

#if RD_DEBUG
    assert(threads == 1);
    threads--;
#endif

    PIN_RWMutexUnlock(&g_rwlock);

    mcslock.unlock(PIN_ThreadId());
}

static /* PIN_FAST_ANALYSIS_CALL */ VOID memRead(THREADID tid, ADDRINT addr, UINT32 size)
{
    if (VERBOSE > 1)
        fprintf(stderr, "R %p/%d", (void *)addr, size);

    // if (g_pindist_do_profile)
    memAccess(addr, size);
}

static VOID /* PIN_FAST_ANALYSIS_CALL */ memWrite(THREADID tid, ADDRINT addr, UINT32 size)
{
    if (VERBOSE > 1)
        fprintf(stderr, "W %p/%d", (void *)addr, size);

    // if (g_pindist_do_profile)
    memAccess(addr, size);
}

// [[maybe_unused]] static /* PIN_FAST_ANALYSIS_CALL */ VOID stackAccess() {
// stackAccesses++; }

/* ===================================================================== */
/* Instrumentation                                                       */
/* ===================================================================== */

VOID Instruction(INS ins, VOID *v)
{
    if (!g_pindist_do_profile) {
        return;
    }

    if (IGNORE_STACK && (INS_IsStackRead(ins) || INS_IsStackWrite(ins))) {
        // INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)stackAccess, IARG_END);
        return;
    }

    // IARG_FAST_ANALYSIS_CALL
    UINT32 memOperands = INS_MemoryOperandCount(ins);

    for (UINT32 memOp = 0; memOp < memOperands; memOp++) {
        if (INS_MemoryOperandIsRead(ins, memOp))
            INS_InsertPredicatedCall(ins,
                                     IPOINT_BEFORE,
                                     (AFUNPTR)memRead,
                                     /*IARG_FAST_ANALYSIS_CALL*/ IARG_THREAD_ID,
                                     IARG_MEMORYOP_EA,
                                     memOp,
                                     IARG_UINT32,
                                     INS_MemoryOperandSize(ins, memOp),
                                     IARG_END);

        if (INS_MemoryOperandIsWritten(ins, memOp))
            INS_InsertPredicatedCall(ins,
                                     IPOINT_BEFORE,
                                     (AFUNPTR)memWrite,
                                     /*IARG_FAST_ANALYSIS_CALL*/ IARG_THREAD_ID,
                                     IARG_MEMORYOP_EA,
                                     memOp,
                                     IARG_UINT32,
                                     INS_MemoryOperandSize(ins, memOp),
                                     IARG_END);
    }
}

/* ===================================================================== */
/* Callbacks from Pin                                                    */
/* ===================================================================== */

VOID ThreadStart(THREADID tid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    fprintf(stderr, "Thread %d started\n", tid);

    PIN_MutexLock(&thread_mutex);
    num_threads++;
    g_max_threads = num_threads > g_max_threads ? num_threads : g_max_threads;
    if (num_threads > MAX_THREADS) {
        /* TODO */
    }
    PIN_MutexUnlock(&thread_mutex);
}

VOID ThreadFini(THREADID tid, const CONTEXT *ctxt, INT32 flags, VOID *v)
{
    fprintf(stderr, "Thread %d finished\n", tid);

    PIN_MutexLock(&thread_mutex);
    num_threads--;
    PIN_MutexUnlock(&thread_mutex);
}

/* ===================================================================== */
/* Output results at exit                                                */
/* ===================================================================== */
extern void print_fnargmap(); // TODO: move to header

VOID Exit(INT32 code, VOID *v)
{
    // fprintf(out, "%s  ignored stack accesses: %lu\n", pStr, stackAccesses);
    // fprintf(out, "%s  ignored accesses by thread != 0: %lu reads, %lu writes\n", pStr,
    // ignoredReads, ignoredWrites);
    RD_print_csv();
    print_fnargmap();
    // free_memory();
}

/* ===================================================================== */
/* Usage/Main Function of the Pin Tool                                   */
/* ===================================================================== */

INT32 Usage()
{
    PIN_ERROR("PinDist: Get the Stack Reuse Distance Histogram\n" +
              KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}

int main(int argc, char *argv[])
{
    if (PIN_Init(argc, argv))
        return Usage();

    PIN_InitSymbols();
    IMG_AddInstrumentFunction(ImageLoad, 0);
    INS_AddInstrumentFunction(Instruction, 0);

    PIN_AddFiniFunction(Exit, 0);
    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddThreadFiniFunction(ThreadFini, 0);

    PIN_RWMutexInit(&g_rwlock);
    PIN_MutexInit(&thread_mutex);

    // required buckets for a64fx:
    // 4-way L1d 64KiB => 4 Buckets with distance 64KiB / 4
    // 16-way L2 8MiB => 16 Buckets with distance 8MiB / 16
    //
    int KiB = 1024;
    int MiB = 1024 * KiB;

    int L1ways = 4;
    int L2ways = 16;

    [[maybe_unused]] int L1d_capacity_per_way = 64 * KiB / 4;
    int                  L2_capacity_per_way  = 8 * MiB / 16;

    Bucket::min_dists.push_back(0);

    for (int i = 0; i < L1ways; i++)
        Bucket::min_dists.push_back(L1d_capacity_per_way * (i + 1) / MEMBLOCKLEN);

    // 12 threads
    // for (int i = 0; i < L1ways; i++)
    //     Bucket::min_dists.push_back(12 * L1d_capacity_per_way * (i + 1) / MEMBLOCKLEN);

    // 48 threads
    // for (int i = 0; i < L1ways; i++)
    //     Bucket::min_dists.push_back(48 * L1d_capacity_per_way * (i + 1) / MEMBLOCKLEN);

    for (int i = 0; i < L2ways; i++)
        Bucket::min_dists.push_back(L2_capacity_per_way * (i + 1) / MEMBLOCKLEN);

    // 4 shared caches (48 threads)
    // for (int i = 0; i < L2ways; i++)
    //     Bucket::min_dists.push_back(4 * L2_capacity_per_way * (i + 1) / MEMBLOCKLEN);

    // bucket for cold misses (infinite reuse distance)
    Bucket::min_dists.push_back(Bucket::inf_dist);

    // sort buckets in ascending order
    std::sort(Bucket::min_dists.begin(), Bucket::min_dists.end());

    // remove duplicates in buckets
    Bucket::min_dists.erase(
        std::unique(std::begin(Bucket::min_dists), std::end(Bucket::min_dists)),
        std::end(Bucket::min_dists));

    RD_init();

    // stackAccesses = 0;

    PIN_StartProgram();
    return 0;
}
