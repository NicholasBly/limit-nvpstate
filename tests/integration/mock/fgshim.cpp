// Test-only shim. Wine 9 never raises EVENT_SYSTEM_FOREGROUND, so the app's
// SetWinEventHook/UnhookWinEvent calls are redirected here (see fgshim.h).
// A 25 ms timer on the hooking thread watches GetForegroundWindow() and calls
// the app's hook proc exactly as Windows would for a foreground change:
// event = EVENT_SYSTEM_FOREGROUND, idObject = OBJID_WINDOW, idChild = CHILDID_SELF.
// Every other event range is passed straight to the real API.
#include <windows.h>

// This file implements the shims, so it must see the real functions.
#undef SetWinEventHook
#undef UnhookWinEvent
#undef OpenProcess
#undef CreateThread
#undef CreateToolhelp32Snapshot
#undef Process32FirstW
#undef Process32NextW

namespace {
WINEVENTPROC appProc   = nullptr;
UINT_PTR     timerId   = 0;
HWND         lastFg    = nullptr;
HWINEVENTHOOK fakeHook = reinterpret_cast<HWINEVENTHOOK>(0x5EED);

void CALLBACK tick(HWND, UINT, UINT_PTR, DWORD) {
    HWND fg = GetForegroundWindow();
    if (fg && fg != lastFg && appProc) {
        lastFg = fg;
        appProc(fakeHook, EVENT_SYSTEM_FOREGROUND, fg, OBJID_WINDOW, CHILDID_SELF, 0, GetTickCount());
    }
}
} // namespace

extern "C" HWINEVENTHOOK WINAPI shim_SetWinEventHook(DWORD eventMin, DWORD eventMax, HMODULE hmod,
                                                     WINEVENTPROC proc, DWORD pid, DWORD tid, DWORD flags) {
    if (eventMin == EVENT_SYSTEM_FOREGROUND && eventMax == EVENT_SYSTEM_FOREGROUND) {
        appProc = proc;
        lastFg  = GetForegroundWindow();   // Windows only reports *changes*
        timerId = SetTimer(nullptr, 0, 25, tick);
        return fakeHook;
    }
    return SetWinEventHook(eventMin, eventMax, hmod, proc, pid, tid, flags);
}

extern "C" BOOL WINAPI shim_UnhookWinEvent(HWINEVENTHOOK h) {
    if (h == fakeHook) {
        if (timerId) KillTimer(nullptr, timerId);
        timerId = 0;
        appProc = nullptr;
        return TRUE;
    }
    return UnhookWinEvent(h);
}

// ---------------------------------------------------------------------------
// Anti-cheat stand-in: when MOCK_BLOCK_QUERY_EXE names an exe, OpenProcess on
// that exe's processes fails for any query/read access (SYNCHRONIZE alone is
// still allowed). Every variant goes through this via fgshim.h.
#include <tlhelp32.h>
#include <cstdlib>
#include <cwchar>

#undef OpenProcess

static bool pidIsBlocked(DWORD pid) {
    const char* env = std::getenv("MOCK_BLOCK_QUERY_EXE");
    if (!env || !*env) return false;
    wchar_t want[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, env, -1, want, MAX_PATH);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    bool blocked = false;
    if (Process32FirstW(snap, &pe)) do {
        if (pe.th32ProcessID == pid) { blocked = _wcsicmp(pe.szExeFile, want) == 0; break; }
    } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return blocked;
}

extern "C" HANDLE WINAPI shim_OpenProcess(DWORD access, BOOL inherit, DWORD pid) {
    const DWORD queryish = PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ;
    if ((access & queryish) && pidIsBlocked(pid)) {
        SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
    }
    return OpenProcess(access, inherit, pid);
}

// ---------------------------------------------------------------------------
// Fault injection and counters for the review-fix round.
//   MOCK_FAIL_HOOK=1        SetWinEventHook(EVENT_SYSTEM_FOREGROUND) fails
//   MOCK_FAIL_HOOK_FILE=p   ...while file p exists
//   MOCK_FAIL_THREAD=1      CreateThread fails
//   MOCK_FAIL_SCAN_FILE=p   while file p exists, Process32FirstW/NextW fail
//                           with ERROR_INVALID_DATA (not ERROR_NO_MORE_FILES)
//   MOCK_COUNT_FILE=p       append "snap" per process snapshot, "hook" per hook
#include <cstdio>
#undef CreateThread
#undef CreateToolhelp32Snapshot
#undef Process32FirstW
#undef Process32NextW

static void countEvent(const char* what) {
    const char* p = std::getenv("MOCK_COUNT_FILE");
    if (!p || !*p) return;
    if (FILE* f = std::fopen(p, "a")) { std::fprintf(f, "%s\n", what); std::fclose(f); }
}

static bool scanShouldFail() {
    const char* p = std::getenv("MOCK_FAIL_SCAN_FILE");
    return p && *p && GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

extern "C" HWINEVENTHOOK WINAPI shim2_SetWinEventHook(DWORD a, DWORD b, HMODULE c, WINEVENTPROC d, DWORD e, DWORD f, DWORD g) {
    if (a == EVENT_SYSTEM_FOREGROUND) {
        countEvent("hook");
        if (std::getenv("MOCK_FAIL_HOOK")) return nullptr;
        // MOCK_FAIL_HOOK_FILE=p: fails while file p exists, so a test can make
        // only a later process (the new version an update starts) fail.
        if (const char* p = std::getenv("MOCK_FAIL_HOOK_FILE")) {
            if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) return nullptr;
        }
    }
    return shim_SetWinEventHook(a, b, c, d, e, f, g);
}

extern "C" HANDLE WINAPI shim_CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T st, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD fl, LPDWORD id) {
    if (std::getenv("MOCK_FAIL_THREAD")) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return nullptr; }
    return CreateThread(sa, st, fn, arg, fl, id);
}

extern "C" HANDLE WINAPI shim_CreateToolhelp32Snapshot(DWORD fl, DWORD pid) {
    countEvent("snap");
    return CreateToolhelp32Snapshot(fl, pid);
}

extern "C" BOOL WINAPI shim_Process32FirstW(HANDLE h, LPPROCESSENTRY32W pe) {
    if (scanShouldFail()) { SetLastError(ERROR_INVALID_DATA); return FALSE; }
    return Process32FirstW(h, pe);
}

extern "C" BOOL WINAPI shim_Process32NextW(HANDLE h, LPPROCESSENTRY32W pe) {
    if (scanShouldFail()) { SetLastError(ERROR_INVALID_DATA); return FALSE; }
    return Process32NextW(h, pe);
}
