// filelock.exe <file> <ms>: hold <file> open without delete/write sharing for
// <ms> -- what a virus scanner reading a file looks like to a rename.
#include <windows.h>
#include <cwchar>
int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    HANDLE h = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 1;
    Sleep(_wtoi(argv[2]));
    CloseHandle(h);
    return 0;
}
