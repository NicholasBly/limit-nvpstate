// Fake nvapi64.dll for testing limit-nvpstate under Wine.
//
// Pretends to be 1..N NVIDIA GPUs and appends one line per
// NvAPI_GPU_SetPstateClientLimits call to the file named by MOCK_NV_LOG.
//
// Env:
//   MOCK_NV_LOG              log path (Windows path, e.g. Z:\tmp\nv.log)
//   MOCK_NV_GPUS             number of GPUs (default 1)
//   MOCK_NV_PSTATES<i>       comma list for GPU i (default "0,2,3,5,8")
//   MOCK_NV_FAIL_SET_AFTER   after this many successful sets, every set fails

#include <windows.h>
#include <nvapi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

CRITICAL_SECTION logLock;
bool             logLockReady = false;
LARGE_INTEGER    t0, freq;
int              setCalls = 0;
int              current[NVAPI_MAX_PHYSICAL_GPUS];

int envInt(const char* name, int fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::atoi(v) : fallback;
}

void logLine(const char* fmt, ...) {
    const char* path = std::getenv("MOCK_NV_LOG");
    if (!path || !*path) return;

    if (!logLockReady) {
        InitializeCriticalSection(&logLock);
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&t0);
        logLockReady = true;
    }

    EnterCriticalSection(&logLock);
    FILE* f = std::fopen(path, "a");
    if (f) {
        // Unix epoch in ms, so the test runner can line it up with its own clock.
        FILETIME ft;
        GetSystemTimePreciseAsFileTime(&ft);
        const unsigned long long t100ns = (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        const double ms = double(t100ns - 116444736000000000ULL) / 10000.0;
        std::fprintf(f, "%.1f pid=%lu tid=%lu ", ms, GetCurrentProcessId(), GetCurrentThreadId());
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(f, fmt, ap);
        va_end(ap);
        std::fputc('\n', f);
        std::fclose(f);
    }
    LeaveCriticalSection(&logLock);
}

int gpuIndex(NvPhysicalGpuHandle h) {
    const int i = int(reinterpret_cast<uintptr_t>(h) - 0x1000);
    return (i >= 0 && i < envInt("MOCK_NV_GPUS", 1)) ? i : -1;
}

std::vector<int> pstatesFor(int gpu) {
    char name[32];
    std::snprintf(name, sizeof(name), "MOCK_NV_PSTATES%d", gpu);
    const char* v = std::getenv(name);
    if (!v || !*v) v = "0,2,3,5,8";

    std::vector<int> out;
    const char* p = v;
    while (*p) {
        out.push_back(std::atoi(p));
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return out;
}

NvAPI_Status __cdecl mockSetPstateClientLimits(NvPhysicalGpuHandle h, unsigned int type, unsigned int limit) {
    const int gpu = gpuIndex(h);

    // MOCK_NV_SET_DELAY_MS: make the driver call slow, to widen race windows.
    const int delay = envInt("MOCK_NV_SET_DELAY_MS", 0);
    if (delay > 0) {
        logLine("SET-BEGIN gpu=%d limit=P%u (slow)", gpu, limit);
        Sleep(DWORD(delay));
    }
    const int failAfter = envInt("MOCK_NV_FAIL_SET_AFTER", -1);

    if (gpu < 0) {
        logLine("SET gpu=? type=%u limit=%u -> BAD HANDLE", type, limit);
        return NVAPI_INVALID_ARGUMENT;
    }

    // MOCK_NV_STRICT: like a real driver might, refuse a limit this GPU does
    // not offer (0 = unlimited is always fine).
    if (envInt("MOCK_NV_STRICT", 0) && limit != 0) {
        bool offered = false;
        for (int p : pstatesFor(gpu)) offered |= (p == int(limit));
        if (!offered) {
            logLine("SET gpu=%d type=%u limit=P%u -> FAIL (not offered by this GPU)", gpu, type, limit);
            return NVAPI_INVALID_ARGUMENT;
        }
    }

    // MOCK_NV_FAIL_LIMIT="g:l,g:l": refuse exactly these (gpu, limit) pairs,
    // e.g. "0:0" = GPU0 cannot be released.
    if (const char* fl = std::getenv("MOCK_NV_FAIL_LIMIT")) {
        char want[16];
        std::snprintf(want, sizeof(want), "%d:%u", gpu, limit);
        const char* p = fl;
        while (*p) {
            const char* e = p;
            while (*e && *e != ',') e++;
            if (size_t(e - p) == std::strlen(want) && std::strncmp(p, want, e - p) == 0) {
                logLine("SET gpu=%d type=%u limit=P%u -> FAIL (MOCK_NV_FAIL_LIMIT)", gpu, type, limit);
                return NVAPI_ERROR;
            }
            p = *e ? e + 1 : e;
        }
    }

    if (failAfter >= 0 && setCalls >= failAfter) {
        logLine("SET gpu=%d type=%u limit=%u -> FAIL (injected)", gpu, type, limit);
        return NVAPI_ERROR;
    }

    setCalls++;
    current[gpu] = int(limit);
    logLine("SET gpu=%d type=%u limit=P%u", gpu, type, limit);
    return NVAPI_OK;
}

} // namespace

extern "C" {

__declspec(dllexport) void* __cdecl nvapi_QueryInterface(unsigned int id) {
    if (id == 0xFDFC7D49u) {
        return reinterpret_cast<void*>(&mockSetPstateClientLimits);
    }
    logLine("QueryInterface unknown id=0x%08X", id);
    return nullptr;
}

} // extern "C"

extern "C" __declspec(dllexport) NvAPI_Status __cdecl NvAPI_Initialize() {
    logLine("Initialize");
    return NVAPI_OK;
}

extern "C" __declspec(dllexport) NvAPI_Status __cdecl NvAPI_EnumPhysicalGPUs(NvPhysicalGpuHandle nvGPUHandle[NVAPI_MAX_PHYSICAL_GPUS], NvU32* pGpuCount) {
    const int n = envInt("MOCK_NV_GPUS", 1);
    for (int i = 0; i < n; i++) {
        nvGPUHandle[i] = reinterpret_cast<NvPhysicalGpuHandle>(uintptr_t(0x1000 + i));
    }
    *pGpuCount = NvU32(n);
    logLine("EnumPhysicalGPUs -> %d", n);
    return NVAPI_OK;
}

extern "C" __declspec(dllexport) NvAPI_Status __cdecl NvAPI_GPU_GetFullName(NvPhysicalGpuHandle hPhysicalGpu, NvAPI_ShortString szName) {
    const int gpu = gpuIndex(hPhysicalGpu);
    std::snprintf(szName, NVAPI_SHORT_STRING_MAX, "Mock GeForce #%d", gpu);
    return gpu < 0 ? NVAPI_INVALID_ARGUMENT : NVAPI_OK;
}

extern "C" __declspec(dllexport) NvAPI_Status __cdecl NvAPI_GPU_GetPstates20(NvPhysicalGpuHandle hPhysicalGpu, NV_GPU_PERF_PSTATES20_INFO* info) {
    const int gpu = gpuIndex(hPhysicalGpu);
    if (gpu < 0 || !info) return NVAPI_INVALID_ARGUMENT;

    const std::vector<int> ps = pstatesFor(gpu);
    info->numPstates = NvU32(ps.size());
    for (size_t i = 0; i < ps.size() && i < NVAPI_MAX_GPU_PSTATE20_PSTATES; i++) {
        info->pstates[i].pstateId = NV_GPU_PERF_PSTATE_ID(ps[i]);
    }
    logLine("GetPstates20 gpu=%d n=%u", gpu, info->numPstates);
    return NVAPI_OK;
}
