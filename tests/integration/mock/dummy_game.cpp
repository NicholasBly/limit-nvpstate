// Tiny stand-in for a game: opens a window titled after its own exe name,
// grabs the foreground, and lives until closed or until argv[1] seconds pass.
#include <windows.h>
#include <shlwapi.h>
#include <cstdlib>

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmd, int) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t* title = PathFindFileNameW(path);
    wchar_t custom[256];
    if (GetEnvironmentVariableW(L"DUMMY_TITLE", custom, 256)) title = custom;

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = inst;
    wc.lpszClassName = L"dummy_game";
    RegisterClassW(&wc);

    // DUMMY_NOFG=1: open behind, like a game launched minimised or in the background.
    const bool noFocus = GetEnvironmentVariableW(L"DUMMY_NOFG", custom, 256) > 0;
    HWND h = CreateWindowExW(noFocus ? WS_EX_NOACTIVATE : 0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                             50, 50, 320, 200, nullptr, nullptr, inst, nullptr);
    ShowWindow(h, noFocus ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (!noFocus) SetForegroundWindow(h);

    // DUMMY_TWO=1: a second window in the same process, titled "<title>#2".
    if (GetEnvironmentVariableW(L"DUMMY_TWO", custom, 256)) {
        wchar_t t2[300];
        wsprintfW(t2, L"%s#2", title);
        HWND h2 = CreateWindowExW(0, wc.lpszClassName, t2, WS_OVERLAPPEDWINDOW, 400, 50, 320, 200, nullptr, nullptr, inst, nullptr);
        ShowWindow(h2, SW_SHOWNOACTIVATE);
    }

    const int seconds = (cmd && *cmd) ? _wtoi(cmd) : 0;
    if (seconds > 0) SetTimer(h, 1, seconds * 1000, [](HWND hw, UINT, UINT_PTR, DWORD) { DestroyWindow(hw); });

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
