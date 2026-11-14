#pragma once

// nvapi.h relies on the SAL annotations (__success, __in, ...) that the
// Windows headers bring in. It used to get them by accident through <atomic>.
#include <Windows.h>
#include <nvapi.h>

// pstateType argument for NvAPI_GPU_SetPstateClientLimits. The original used a
// bare literal 3 in two places.
inline constexpr unsigned int NV_PSTATE_CLIENT_LIMIT_TYPE = 3;

int initNvAPI();

// Raw NvAPI_GPU_SetPstateClientLimits: `limit` is the P-State number to cap
// at, 0 removes the cap. Returns 0 on success.
//
// No caching here. limitnvpstate.cpp remembers what each GPU was last set to,
// per GPU, under the same lock that decides the target. The earlier single
// "is unlimited" flag was shared by every GPU and could not tell P3 from P8.
int setPStateLimit(NvPhysicalGpuHandle hPhysicalGpu, unsigned int limit);
