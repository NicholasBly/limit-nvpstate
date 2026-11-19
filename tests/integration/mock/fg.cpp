// fg.exe <window title>  : bring a top-level window to the foreground.
// fg.exe --query         : print the foreground window's title and pid.
#include <windows.h>
#include <cstdio>
#include <cwchar>

int wmain(int argc, wchar_t** argv) {
    if (argc >= 2 && wcscmp(argv[1], L"--query") == 0) {
        HWND h = GetForegroundWindow();
        wchar_t t[256] = {};
        DWORD pid = 0;
        if (h) { GetWindowTextW(h, t, 256); GetWindowThreadProcessId(h, &pid); }
        wprintf(L"fg hwnd=%p pid=%lu title=%ls\n", (void*)h, pid, t);
        return 0;
    }
    if (argc >= 2 && wcscmp(argv[1], L"--list") == 0) {
        EnumWindows([](HWND h, LPARAM) -> BOOL {
            wchar_t t[128] = {}, c[128] = {}; DWORD pid = 0;
            GetWindowTextW(h, t, 128); GetClassNameW(h, c, 128); GetWindowThreadProcessId(h, &pid);
            wprintf(L"%p pid=%lu vis=%d class=%ls title=%ls\n", (void*)h, pid, (int)IsWindowVisible(h), c, t);
            return TRUE;
        }, 0);
        return 0;
    }
    // fg.exe --post <title> <msg> <wparam>: PostMessage to a (possibly hidden) window.
    if (argc >= 5 && wcscmp(argv[1], L"--post") == 0) {
        HWND h = FindWindowW(nullptr, argv[2]);
        if (!h) { wprintf(L"not found: %ls\n", argv[2]); return 1; }
        BOOL ok = PostMessageW(h, (UINT)wcstoul(argv[3], nullptr, 0), (WPARAM)wcstoul(argv[4], nullptr, 0), 0);
        wprintf(L"posted=%d\n", ok);
        return ok ? 0 : 1;
    }
    if (argc < 2) return 2;
    HWND h = FindWindowW(nullptr, argv[1]);
    if (!h) { wprintf(L"not found: %ls\n", argv[1]); return 1; }
    ShowWindow(h, SW_RESTORE);
    BOOL ok = SetForegroundWindow(h);
    wprintf(L"SetForegroundWindow(%ls) = %d\n", argv[1], ok);
    return ok ? 0 : 1;
}
