#pragma once

// Test builds only (the Wine test rig defines LIMITNV_TEST_HOOKS): widen a
// timing window on purpose, shorten a wait, or make one step fail, so the
// rig can check what happens every run instead of by luck. In real builds
// all of this compiles to nothing.

#include <string>

#ifdef LIMITNV_TEST_HOOKS

#include <Windows.h>

#include <cwchar>

// Sleeps for the number of milliseconds in the environment variable, if set.
inline void testPause(const wchar_t* variable) {
    wchar_t ms[16];

    if (GetEnvironmentVariableW(variable, ms, 16) > 0) {
        Sleep(static_cast<DWORD>(_wtoi(ms)));
    }
}

// The variable's number, or `normal` when it isn't set.
inline unsigned testValue(const wchar_t* variable, unsigned normal) {
    wchar_t value[16];
    return GetEnvironmentVariableW(variable, value, 16) > 0 ? static_cast<unsigned>(_wtoi(value)) : normal;
}

// True when the variable is set to exactly `what` (e.g. a file name), or to
// "*": that step should behave as if it failed.
inline bool testFails(const wchar_t* variable, const std::wstring& what) {
    wchar_t value[512];
    const DWORD n = GetEnvironmentVariableW(variable, value, 512);
    return n > 0 && n < 512 && (what == value || std::wstring(value) == L"*");
}

// The variable's text, if it is set.
inline bool testText(const wchar_t* variable, std::wstring& out) {
    wchar_t     value[512];
    const DWORD n = GetEnvironmentVariableW(variable, value, 512);

    if (n == 0 || n >= 512) {
        return false;
    }

    out.assign(value, n);
    return true;
}

#else

inline bool     testText(const wchar_t*, std::wstring&) { return false; }
inline void     testPause(const wchar_t*) {}
inline unsigned testValue(const wchar_t*, unsigned normal) { return normal; }
inline bool     testFails(const wchar_t*, const std::wstring&) { return false; }

#endif
