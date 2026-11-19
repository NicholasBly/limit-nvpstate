// winlist.exe [close]: prints every visible dialog (message box) as
// "title|text" lines; with "close", also closes them.
#include <windows.h>
#include <cstdio>
#include <string>

static BOOL CALLBACK child(HWND w, LPARAM out) {
    wchar_t cls[64], text[2048];
    GetClassNameW(w, cls, 64);
    if (lstrcmpiW(cls, L"Static") == 0 && InternalGetWindowText(w, text, 2048) > 0) {
        *reinterpret_cast<std::wstring*>(out) += text;
        *reinterpret_cast<std::wstring*>(out) += L" ";
    }
    return TRUE;
}

static BOOL CALLBACK top(HWND w, LPARAM close) {
    wchar_t cls[64], title[256];
    GetClassNameW(w, cls, 64);
    if (IsWindowVisible(w) && lstrcmpW(cls, L"#32770") == 0) {
        InternalGetWindowText(w, title, 256);
        std::wstring text;
        EnumChildWindows(w, child, reinterpret_cast<LPARAM>(&text));
        for (wchar_t& c : text) if (c == L'\n' || c == L'\r') c = L' ';
        std::printf("%ls|%ls\n", title, text.c_str());
        if (close) PostMessageW(w, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

int wmain(int argc, wchar_t** argv) {
    EnumWindows(top, argc > 1 && lstrcmpW(argv[1], L"close") == 0);
    return 0;
}
