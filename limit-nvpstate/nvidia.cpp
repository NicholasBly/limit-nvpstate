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

} // namespace

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

int setPStateLimit(NvPhysicalGpuHandle hPhysicalGpu, unsigned int limit) {
    if (!pfnSetPstateLimits || !hPhysicalGpu) {
        return 1;
    }

    if (pfnSetPstateLimits(hPhysicalGpu, NV_PSTATE_CLIENT_LIMIT_TYPE, limit) != 0) {
        LOG(L"error: NvAPI_GPU_SetPstateClientLimits(P%u) failed\n", limit);
        return 1;
    }

    LOG(L"info: set P%u\n", limit);
    return 0;
}
