#include <windows.h>
#include <cstdio>
static void CALLBACK proc(HWINEVENTHOOK, DWORD ev, HWND h, LONG o, LONG c, DWORD, DWORD) {
    wchar_t t[128] = {}; GetWindowTextW(h, t, 128); DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    wprintf(L"EVENT fg hwnd=%p obj=%ld child=%ld pid=%lu title=%ls\n", (void*)h, o, c, pid, t); fflush(stdout);
}
int main() {
    HWINEVENTHOOK hk = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, proc, 0, 0, WINEVENT_OUTOFCONTEXT);
    printf("hook=%p\n", (void*)hk); fflush(stdout);
    DWORD end = GetTickCount() + 12000; MSG m;
    while (GetTickCount() < end) { while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m); Sleep(20); }
}
