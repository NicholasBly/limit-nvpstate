// Before/after driver for utils.cpp (startup entry + process name lookup).
// Built once per variant against that variant's utils.cpp.
#include <utils.h>

#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>

// -DOpenProcess=test_OpenProcess redirects utils.cpp's calls here, so we can
// play an anti-cheat driver that refuses query access to one pid.
DWORD blockedPid = 0;
extern "C" HANDLE WINAPI test_OpenProcess(DWORD access, BOOL inherit, DWORD pid) {
    using Fn = HANDLE (WINAPI*)(DWORD, BOOL, DWORD);
    static Fn real = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "OpenProcess"));
    if (pid == blockedPid && (access & (PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ))) {
        SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
    }
    return real(access, inherit, pid);
}

static const wchar_t* RUN = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";

static bool rawExists(REGSAM view) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, RUN, 0, KEY_QUERY_VALUE | view, &k) != ERROR_SUCCESS) return false;
    LSTATUS r = RegQueryValueExW(k, L"limit-nvpstate", nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}
static void rawDelete(REGSAM view) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, RUN, 0, KEY_SET_VALUE | view, &k) == ERROR_SUCCESS) {
        RegDeleteValueW(k, L"limit-nvpstate");
        RegCloseKey(k);
    }
}
// Exactly what 5.2.1 wrote: literal Wow6432Node path, unquoted value.
static void writeLegacyLike521() {
    HKEY k;
    RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Wow6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &k);
    const char v[] = "C:\\Tools\\limit-nvpstate\\limit-nvpstate.exe";
    RegSetValueExA(k, "limit-nvpstate", 0, REG_SZ, (const BYTE*)v, sizeof(v));
    RegCloseKey(k);
}
static void state(const char* label) {
    std::printf("  %-44s isAddedToStartup=%d  entries: 64-bit=%d legacy=%d\n",
                label, (int)isAddedToStartup(), (int)rawExists(KEY_WOW64_64KEY), (int)rawExists(KEY_WOW64_32KEY));
}

int main() {
    std::printf("[startup] registry views distinct in this prefix: ");
    rawDelete(KEY_WOW64_64KEY); rawDelete(KEY_WOW64_32KEY);
    writeLegacyLike521();
    std::printf("%s\n", (rawExists(KEY_WOW64_32KEY) && !rawExists(KEY_WOW64_64KEY)) ? "yes" : "NO (test invalid)");

    std::printf("[startup] A: user upgraded from 5.2.1 with Add To Startup ticked\n");
    state("after upgrade (menu should show ticked)");
    std::printf("  addToStartup(false) -> %d\n", (int)addToStartup(false));
    state("after unticking (should be gone everywhere)");

    std::printf("[startup] B: upgraded, then ticks it again\n");
    rawDelete(KEY_WOW64_64KEY); rawDelete(KEY_WOW64_32KEY); writeLegacyLike521();
    std::printf("  addToStartup(true) -> %d\n", (int)addToStartup(true));
    state("after ticking (should be exactly one entry)");

    std::printf("[startup] C: fresh install\n");
    rawDelete(KEY_WOW64_64KEY); rawDelete(KEY_WOW64_32KEY);
    state("fresh");
    std::printf("  addToStartup(true) -> %d\n", (int)addToStartup(true));
    state("ticked");
    std::printf("  addToStartup(false) -> %d\n", (int)addToStartup(false));
    state("unticked");
    rawDelete(KEY_WOW64_64KEY); rawDelete(KEY_WOW64_32KEY);

    // ---- process names ----
    wchar_t name[MAX_PATH];
    const DWORD self = GetCurrentProcessId();

    std::printf("[names] own pid, normal:            ok=%d name=%ls\n", (int)getProcessNameByPID(self, name, MAX_PATH), name);
    blockedPid = self;
    std::printf("[names] own pid, anti-cheat blocks: ok=%d name=%ls\n", (int)getProcessNameByPID(self, name, MAX_PATH), name);
    blockedPid = 0;
    std::printf("[names] pid 0:                      ok=%d\n", (int)getProcessNameByPID(0, name, MAX_PATH));
    std::printf("[names] dead pid 0x7ffffff0:        ok=%d\n", (int)getProcessNameByPID(0x7ffffff0, name, MAX_PATH));
    return 0;
}
// utils.cpp sees OpenProcess (renamed) as a dllimport, so it calls through
// __imp_<name>. Point that slot at our stand-in.
extern "C" { void* __imp_test_OpenProcess = reinterpret_cast<void*>(&test_OpenProcess); }
