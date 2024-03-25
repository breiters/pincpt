#include "imgload.h"

#include "bucket.h"
#include "config.h"
#include "datastructs.h"
#include "debug_reader.h"
#include "region.h"

#include "pin.H"

#include <iostream>
#include <string>
// #include <unordered_map>
#include <vector>

extern PIN_RWMUTEX g_rwlock;
extern void        inspect_ptr_args(RTN rtn, std::string fname2);

std::string g_application_name;

void handle_datastruct(ADDRINT returnIp, Datastruct &ds)
{
    PIN_RWMutexWriteLock(&g_rwlock);

    PIN_LockClient();
    LEVEL_PINCLIENT::PIN_GetSourceLocation(
        ADDRINT(returnIp), &ds.col, &ds.line, &ds.file_name);
    PIN_UnlockClient();
    // TODO: case file_name length == 0 ?
    if (ds.file_name.length() > 0 && ds.nbytes > RD_DATASTRUCT_THRESHOLD) {
        Datastruct::register_datastruct(ds);
        // ds.print();
    }
    PIN_RWMutexUnlock(&g_rwlock);
}

//  Replace an original function with a custom function defined in the tool
//  using probes.  The replacement function has a different signature from that
//  of the original replaced function.

typedef VOID *(*FP_MALLOC)(size_t);
// This is the replacement routine.
VOID *NewMalloc(FP_MALLOC orgFuncptr, UINT64 arg0, ADDRINT returnIp)
{
    Datastruct ds;
    // Call the relocated entry point of the original (replaced) routine.
    ds.address   = orgFuncptr(arg0);
    ds.nbytes    = arg0;
    ds.allocator = "malloc";

    handle_datastruct(returnIp, ds);

    return ds.address;
}

typedef void *(*fp_calloc)(size_t, size_t);
VOID *NewCalloc(fp_calloc orgFuncptr, UINT64 arg0, UINT64 arg1, ADDRINT returnIp)
{
    Datastruct ds;
    ds.address   = orgFuncptr(arg0, arg1);
    ds.nbytes    = arg0 * arg1;
    ds.allocator = "calloc";

    handle_datastruct(returnIp, ds);

    return ds.address;
}

typedef void *(*fp_aligned_alloc)(size_t, size_t);
VOID *
NewAlignedAlloc(fp_aligned_alloc orgFuncptr, UINT64 arg0, UINT64 arg1, ADDRINT returnIp)
{
    Datastruct ds;
    VOID      *ret = orgFuncptr(arg0, arg1);
    ds.address     = ret;
    ds.nbytes      = arg1;
    ds.allocator   = "aligned_alloc";

    handle_datastruct(returnIp, ds);

    return ret;
}

typedef int (*fp_posix_memalign)(void **, size_t, size_t);
INT32 NewPosixMemalign(fp_posix_memalign orgFuncptr,
                       VOID            **memptr,
                       UINT64            arg0,
                       UINT64            arg1,
                       ADDRINT           returnIp)
{
    Datastruct ds;
    int        ret = orgFuncptr(memptr, arg0, arg1);
    ds.address     = *memptr;
    ds.nbytes      = arg1;
    ds.allocator   = "posix_memalign";

    handle_datastruct(returnIp, ds);

    return ret;
}

typedef void (*fp_free)(void *);
VOID NewFree(fp_free orgFuncptr, VOID *arg0, ADDRINT returnIp)
{
    int ds_idx                               = Datastruct::datastruct_num(arg0);
    Datastruct::datastructs[ds_idx].is_freed = true;
    orgFuncptr(arg0);
    return;
}

/**
 *   Replacement functions for region markers
 */
typedef void (*fp_pindist_start_stop)(char *);
VOID PINDIST_start_region_(char *region)
{
    PIN_RWMutexWriteLock(&g_rwlock);

    auto reg = g_regions.find(region);

    if (reg == g_regions.end()) {
        g_regions[region] = new Region(region);
        g_regions[region]->on_region_entry();
    } else {
        reg->second->on_region_entry();
    }

    PIN_RWMutexUnlock(&g_rwlock);
}

VOID New_PINDIST_start_region(fp_pindist_start_stop orgFuncptr,
                              char                 *region,
                              ADDRINT               returnIp)
{
    PINDIST_start_region_(region);
}

VOID PINDIST_stop_region_(char *region)
{
    PIN_RWMutexWriteLock(&g_rwlock);

    auto reg = g_regions.find(region);
    assert(reg != g_regions.end());
    if (reg != g_regions.end()) {
        reg->second->on_region_exit();
    }

    PIN_RWMutexUnlock(&g_rwlock);
}

VOID New_PINDIST_stop_region(fp_pindist_start_stop orgFuncptr,
                             char                 *region,
                             ADDRINT               returnIp)
{
    PINDIST_stop_region_(region);
}

extern bool g_pindist_do_profile;
VOID        New_PINDIST_start_profile()
{
    std::cout << "starting profiling..." << std::endl;
    g_pindist_do_profile = true;
}
VOID New_PINDIST_stop_profile()
{
    std::cout << "stopping profiling..." << std::endl;
    g_pindist_do_profile = false;
}

/**
 * @brief Helper class to replace a function in an image.
 *
 * @tparam ReturnType
 * @tparam Targs
 */
template <typename ReturnType, typename... Targs>
class Prototype
{
public:
    Prototype(const char *name) : proto{make_proto(name)} {}

    template <size_t Y, size_t X, size_t... Expand>
    struct recursive_tclass {
        typedef typename recursive_tclass<Y, (X - 1), Y, X, Expand...>::type type;
    };

    template <size_t... Expand>
    struct recursive_tclass<IARG_FUNCARG_ENTRYPOINT_VALUE, 0, Expand...> {
        typedef recursive_tclass<IARG_FUNCARG_ENTRYPOINT_VALUE, 0, Expand...> type;
        static void replace_signature(RTN rtn, AFUNPTR fun_ptr, PROTO proto)
        {
            // puts(__PRETTY_FUNCTION__);
            RTN_ReplaceSignature(rtn,
                                 fun_ptr,
                                 IARG_PROTOTYPE,
                                 proto,
                                 IARG_ORIG_FUNCPTR,
                                 IARG_FUNCARG_ENTRYPOINT_VALUE,
                                 0,
                                 IARG_RETURN_IP,
                                 Expand...,
                                 IARG_END);
        }
    };

    static void replace_signature(RTN rtn, const char *replace_fn_name, AFUNPTR fun_ptr)
    {
        PROTO proto = PROTO_Allocate(PIN_PARG(ReturnType),
                                     CALLINGSTD_DEFAULT,
                                     replace_fn_name,
                                     PIN_PARG(Targs)...,
                                     PIN_PARG_END());
        recursive_tclass<IARG_FUNCARG_ENTRYPOINT_VALUE,
                         sizeof...(Targs)>::type::replace_signature(rtn, fun_ptr, proto);
        PROTO_Free(proto);
    }

    static PROTO make_proto(const char *name)
    {
        return PROTO_Allocate(PIN_PARG(ReturnType),
                              CALLINGSTD_DEFAULT,
                              name,
                              PIN_PARG(Targs)...,
                              PIN_PARG_END());
    }
    PROTO proto;
};

// Pin calls this function every time a new img is loaded.
// It is best to do probe replacement when the image is loaded,
// because only one thread knows about the image at this time.
//
VOID ImageLoad(IMG img, VOID *v)
{
    std::cout << IMG_Name(img) << std::endl;

    if (IMG_IsMainExecutable(img)) {
        read_debug(IMG_Name(img).c_str());
        std::size_t found = IMG_Name(img).rfind('/');
        if (found != std::string::npos) {
            g_application_name = IMG_Name(img).substr(found + 1);
        } else {
            g_application_name = IMG_Name(img);
        }
        std::cout << "[PINCPT] Profiling: " << g_application_name << std::endl;

        for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
            if (SEC_TYPE_EXEC != SEC_Type(sec)) {
                continue; // LEVEL_CORE::SEC_TYPE
            }
            // printf("sec type: %d\n", (int)SEC_Type(sec));
            for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
                RTN_Open(rtn);
                // do not insert call for stdlib functions:
                LOG("function in img %s: %s\n" + IMG_Name(img) + RTN_Name(rtn));
                if (
#if 0
                    RTN_Name(rtn).rfind("omp_", 0) == 0 ||
                    RTN_Name(rtn).rfind('.', 0) == 0 ||
#endif
                    RTN_Name(rtn).rfind("static_initialization_and_destruction") !=
                        std::string::npos ||
                    RTN_Name(rtn).compare("main") == 0 ||
                    RTN_Name(rtn).compare("register_tm_clones") == 0 ||
                    RTN_Name(rtn).compare("deregister_tm_clones") == 0 ||
                    RTN_Name(rtn).compare("frame_dummy") == 0 ||
                    (RTN_Name(rtn).rfind('_', 0) == 0 &&
                     RTN_Name(rtn).rfind('Z', 1) != 1) || // not mangled
                    RTN_Name(rtn).rfind('@') != std::string::npos ||
                    RTN_Name(rtn).rfind("._omp_fn.") != std::string::npos ||
                    RTN_Name(rtn).rfind("PINDIST_start_region") != std::string::npos ||
                    RTN_Name(rtn).rfind("PINDIST_stop_region") != std::string::npos ||
                    RTN_Name(rtn).rfind("PINDIST_start_profile") != std::string::npos ||
                    RTN_Name(rtn).rfind("PINDIST_stop_profile") != std::string::npos) {
                    RTN_Close(rtn);
                    continue;
                }
#if 0
                /** application functions to not instrument: TODO: remove in production and read from file **/
                if (RTN_Name(rtn).rfind("timer_") != std::string::npos || RTN_Name(rtn).rfind("wtime") != std::string::npos ||
                    RTN_Name(rtn).rfind("c_print_results") != std::string::npos ||
                    RTN_Name(rtn).rfind("alloc_") != std::string::npos || RTN_Name(rtn).rfind("verify") != std::string::npos) {
                    RTN_Close(rtn);
                    continue;
                }
#endif
                LOG("create region for function: %s\n" + RTN_Name(rtn));
                RTN_InsertCall(rtn,
                               IPOINT_BEFORE,
                               (AFUNPTR)PINDIST_start_region_,
                               IARG_PTR,
                               RTN_Name(rtn).c_str(),
                               IARG_END);
                inspect_ptr_args(rtn, RTN_Name(rtn));
                RTN_InsertCall(rtn,
                               IPOINT_AFTER,
                               (AFUNPTR)PINDIST_stop_region_,
                               IARG_PTR,
                               RTN_Name(rtn).c_str(),
                               IARG_END);
                RTN_Close(rtn);
            }
        }
    }

    const char *replace_fn_name;
    RTN         rtn;

    // See if malloc() is present in the image.  If so, replace it.
    replace_fn_name = "malloc";
    rtn             = RTN_FindByName(img, replace_fn_name);
    if (RTN_Valid(rtn)) {
        std::cout << "Replacing " << replace_fn_name << " in " << IMG_Name(img)
                  << std::endl;
        // Define a function prototype that describes the application routine
        // that will be replaced.
        Prototype<void *, size_t>::replace_signature(
            rtn, replace_fn_name, AFUNPTR(NewMalloc));
        // Free the function prototype.
    }

    rtn = RTN_FindByName(img, "calloc");
    if (RTN_Valid(rtn)) {
        std::cout << "Replacing calloc in " << IMG_Name(img) << std::endl;

        Prototype<void *, size_t, size_t>::replace_signature(
            rtn, "calloc", AFUNPTR(NewCalloc));
    }

    rtn = RTN_FindByName(img, "posix_memalign");
    if (RTN_Valid(rtn)) {
        std::cout << "Replacing posix_memalign in " << IMG_Name(img) << std::endl;

        Prototype<int, void **, size_t, size_t>::replace_signature(
            rtn, "posix_memalign", AFUNPTR(NewPosixMemalign));
    }

    rtn = RTN_FindByName(img, "aligned_alloc");
    if (RTN_Valid(rtn)) {
        std::cout << "Replacing aligned_alloc in " << IMG_Name(img) << std::endl;

        Prototype<void *, size_t, size_t>::replace_signature(
            rtn, "aligned_alloc", AFUNPTR(NewAlignedAlloc));
    }

    rtn = RTN_FindByName(img, "free");

    if (RTN_Valid(rtn)) {
        std::cout << "Replacing free in " << IMG_Name(img) << std::endl;

        Prototype<void, void *>::replace_signature(rtn, "free", AFUNPTR(NewFree));
    }

    rtn = RTN_FindByName(img, "PINDIST_start_region");
    if (RTN_Valid(rtn)) {
        std::cout << "Replacing PINDIST_start_region in " << IMG_Name(img) << std::endl;

        Prototype<void, char *>::replace_signature(
            rtn, "PINDIST_start_region", AFUNPTR(New_PINDIST_start_region));
    }

    rtn = RTN_FindByName(img, "PINDIST_stop_region");
    if (RTN_Valid(rtn)) {
        std::cout << "Replacing PINDIST_stop_region in " << IMG_Name(img) << std::endl;

        Prototype<void, char *>::replace_signature(
            rtn, "PINDIST_stop_region", AFUNPTR(New_PINDIST_stop_region));
    }

    replace_fn_name = "PINDIST_start_profile";
    rtn             = RTN_FindByName(img, replace_fn_name);
    if (RTN_Valid(rtn)) {
        std::cout << "Replacing " << replace_fn_name << " in " << IMG_Name(img)
                  << std::endl;

        Prototype<void>::replace_signature(
            rtn, replace_fn_name, AFUNPTR(New_PINDIST_start_profile));
    }

    replace_fn_name = "PINDIST_stop_profile";
    rtn             = RTN_FindByName(img, replace_fn_name);
    if (RTN_Valid(rtn)) {
        std::cout << "Replacing " << replace_fn_name << " in " << IMG_Name(img)
                  << std::endl;

        Prototype<void>::replace_signature(
            rtn, replace_fn_name, AFUNPTR(New_PINDIST_stop_profile));
    }
}
