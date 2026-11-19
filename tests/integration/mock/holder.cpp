// holder.exe <exe> <seconds>: start <exe>, wait for it to exit, then keep its
// process handle open for <seconds> (a "zombie"), printing whether a toolhelp
// snapshot still lists it.
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cwchar>
static bool listed(DWORD pid) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe); bool f = false;
    if (Process32FirstW(s, &pe)) do { if (pe.th32ProcessID == pid) f = true; } while (Process32NextW(s, &pe));
    CloseHandle(s); return f;
}
int wmain(int argc, wchar_t** argv) {
    STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi;
    wchar_t cmd[512]; swprintf(cmd, 512, L"\"%ls\" 2", argv[1]);
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return 1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    printf("exited; still listed in snapshot: %d\n", (int)listed(pi.dwProcessId)); fflush(stdout);
    Sleep(_wtoi(argv[2]) * 1000);
    CloseHandle(pi.hProcess); return 0;
}
