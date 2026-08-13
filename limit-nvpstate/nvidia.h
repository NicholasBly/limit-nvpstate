#pragma once

#include <atomic>
#include <nvapi.h>

// pstateType argument for NvAPI_GPU_SetPstateClientLimits. The original used a
// bare literal 3 in two places.
inline constexpr unsigned int NV_PSTATE_CLIENT_LIMIT_TYPE = 3;

// Cached view of the current limit state, used to skip redundant NVAPI calls.
// Atomic because both the GUI thread and the process polling thread read and
// write it. In the original this was a plain bool touched from both -- a data
// race that could leave the cache disagreeing with the driver.
extern std::atomic<bool> isPStateUnlimited;

int initNvAPI();

// Sets or releases the P-State limit on one GPU.
//   isUnlimit   true  -> release the limit (P0 available)
//               false -> clamp to pStateLimit
//   force       bypass the cache and always issue the NVAPI call. Needed when
//               changing the limit while already limited, or when switching
//               GPUs (where the cache refers to the other GPU).
//
// Returns 0 on success.
//
// NOTE: the default argument lives here and here only. The original repeated
// `= 0` in the definition in nvidia.cpp, which is ill-formed; it compiled only
// because nvidia.cpp never included this header.
int setPState(NvPhysicalGpuHandle hPhysicalGpu,
              bool                isUnlimit,
              unsigned int        pStateLimit = 0,
              bool                force       = false);
