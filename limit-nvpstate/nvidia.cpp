#include <nvidia.h>
#include <log.h>

#include <Windows.h>

typedef void* (*NvAPIQueryPtr)(unsigned int);
typedef NvAPI_Status (*NvAPISetPstateClientLimitsPtr)(NvPhysicalGpuHandle, unsigned int, unsigned int);

namespace {

// Undocumented entry point id for NvAPI_GPU_SetPstateClientLimits.
constexpr unsigned int NVAPI_ID_SET_PSTATE_CLIENT_LIMITS = 0xFDFC7D49;

HMODULE                       hNvAPI               = nullptr;
NvAPISetPstateClientLimitsPtr pfnSetPstateLimits   = nullptr;

// Serialises the read-modify-write on isPStateUnlimited together with the
// NVAPI call itself. Uncontended acquisition is a few nanoseconds and this is
// called at most a handful of times per minute.
SRWLOCK pStateLock = SRWLOCK_INIT;

} // namespace

std::atomic<bool> isPStateUnlimited{ true };

int initNvAPI() {
    if (NvAPI_Initialize() != 0) {
        LOG(L"error: NvAPI_Initialize failed\n");
        return 1;
    }

    // nvapi64.lib has already caused the DLL to be loaded, so take a handle to
    // the existing mapping rather than paying for a second LoadLibrary and an
    // extra refcount that is never released.
    hNvAPI = GetModuleHandleW(L"nvapi64.dll");

    if (!hNvAPI) {
        hNvAPI = LoadLibraryW(L"nvapi64.dll");
    }

    if (!hNvAPI) {
        LOG(L"error: could not obtain a handle to nvapi64.dll\n");
        return 1;
    }

    const NvAPIQueryPtr query =
        reinterpret_cast<NvAPIQueryPtr>(GetProcAddress(hNvAPI, "nvapi_QueryInterface"));

    if (!query) {
        LOG(L"error: nvapi_QueryInterface not exported\n");
        return 1;
    }

    // Resolve once, up front. The original resolved lazily inside a function
    // called from two threads, writing a shared static without synchronisation.
    pfnSetPstateLimits =
        reinterpret_cast<NvAPISetPstateClientLimitsPtr>(query(NVAPI_ID_SET_PSTATE_CLIENT_LIMITS));

    if (!pfnSetPstateLimits) {
        LOG(L"error: could not resolve NvAPI_GPU_SetPstateClientLimits\n");
        return 1;
    }

    return 0;
}

int setPState(NvPhysicalGpuHandle hPhysicalGpu, bool isUnlimit, unsigned int pStateLimit, bool force) {
    if (!pfnSetPstateLimits || !hPhysicalGpu) {
        return 1;
    }

    // Fast path: already in the requested state and no forced re-apply. This is
    // the common case when alt-tabbing between two non-excepted applications,
    // and it avoids touching the lock at all.
    if (!force && isPStateUnlimited.load(std::memory_order_acquire) == isUnlimit) {
        return 0;
    }

    AcquireSRWLockExclusive(&pStateLock);

    int result = 0;

    if (force || isPStateUnlimited.load(std::memory_order_relaxed) != isUnlimit) {
        const unsigned int limit = isUnlimit ? 0u : pStateLimit;

        if (pfnSetPstateLimits(hPhysicalGpu, NV_PSTATE_CLIENT_LIMIT_TYPE, limit) != 0) {
            LOG(L"error: NvAPI_GPU_SetPstateClientLimits failed\n");
            result = 1;
        } else {
            LOG(L"info: set P%u\n", limit);
            isPStateUnlimited.store(isUnlimit, std::memory_order_release);
        }
    }

    ReleaseSRWLockExclusive(&pStateLock);

    return result;
}
