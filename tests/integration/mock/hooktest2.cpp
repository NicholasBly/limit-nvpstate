#include <windows.h>
#include <cstdio>
static void CALLBACK proc(HWINEVENTHOOK, DWORD ev, HWND h, LONG o, LONG c, DWORD, DWORD) {
    wchar_t t[128] = {}; GetWindowTextW(h, t, 128);
    wprintf(L"EVENT %lx hwnd=%p obj=%ld title=%ls\n", ev, (void*)h, o, t); fflush(stdout);
}
int main() {
    HWINEVENTHOOK hk = SetWinEventHook(EVENT_MIN, EVENT_MAX, nullptr, proc, 0, 0, WINEVENT_OUTOFCONTEXT);
    printf("hook=%p\n", (void*)hk); fflush(stdout);
    HWND a = CreateWindowExW(0, L"STATIC", L"winA", WS_OVERLAPPEDWINDOW|WS_VISIBLE, 0,0,200,200, 0,0,0,0);
    HWND b = CreateWindowExW(0, L"STATIC", L"winB", WS_OVERLAPPEDWINDOW|WS_VISIBLE, 300,0,200,200, 0,0,0,0);
    MSG m;
    for (int i = 0; i < 6; i++) { SetForegroundWindow(i%2? a : b);
      DWORD end = GetTickCount() + 500; while (GetTickCount() < end) { while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m); Sleep(10);} }
}
