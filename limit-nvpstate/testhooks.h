#pragma once

// Test builds only (the Wine test rig defines LIMITNV_TEST_HOOKS): widen a
// timing window on purpose, so a race the rig is checking for happens every
// run instead of by luck. In real builds this compiles to nothing.

#ifdef LIMITNV_TEST_HOOKS

#include <Windows.h>

#include <cwchar>

inline void testPause(const wchar_t* variable) {
    wchar_t ms[16];

    if (GetEnvironmentVariableW(variable, ms, 16) > 0) {
        Sleep(static_cast<DWORD>(_wtoi(ms)));
    }
}

#else

inline void testPause(const wchar_t*) {}

#endif
