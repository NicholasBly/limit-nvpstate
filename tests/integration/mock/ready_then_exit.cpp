// ready_then_exit.exe: a "new version" that says it is up (sets the
// --after-update event) and then exits straight away -- the case where
// "ready" alone would wrongly count as a healthy install.
#include <windows.h>
#include <cwchar>
#include <cstdint>
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    // Every number after --after-update: the helper passes one (the event),
    // the 3eabc96 build passed two (process, then event). SetEvent on a
    // handle that isn't an event just fails.
    bool after = false;
    for (int i = 1; args && i < count; i++) {
        if (wcscmp(args[i], L"--after-update") == 0) { after = true; continue; }
        wchar_t* end = nullptr;
        const unsigned long long value = wcstoull(args[i], &end, 10);
        if (after && end && *end == 0 && value) SetEvent(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value)));
    }
    Sleep(300);
    return 0;
}
