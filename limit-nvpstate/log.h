#pragma once

// ---------------------------------------------------------------------------
// Logging
//
// The program links as /SUBSYSTEM:WINDOWS, so there is no console attached and
// every `std::cout << ...` in the original still paid for the full iostream
// sentry/locale machinery before writing to an invalid handle. In the process
// polling loop that ran once per process per poll -- several hundred formatted
// inserts every interval, all discarded.
//
// In Release this macro expands to nothing, which also keeps <iostream> (and
// with it std::ios_base::Init plus the locale facets) out of the binary.
// In Debug it writes to the debugger via OutputDebugStringW, which works in a
// GUI subsystem process where stdout does not. Use DebugView or the Visual
// Studio Output window to read it.
// ---------------------------------------------------------------------------

#ifdef _DEBUG

#include <Windows.h>
#include <cstdarg>
#include <cwchar>

inline void logDebugW(_In_z_ _Printf_format_string_ const wchar_t* format, ...) {
    wchar_t buffer[512];

    va_list args;
    va_start(args, format);
    _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, format, args);
    va_end(args);

    OutputDebugStringW(buffer);
}

#define LOG(...) logDebugW(__VA_ARGS__)

#else

#define LOG(...) ((void)0)

#endif
