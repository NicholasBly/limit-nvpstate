// Test-only stand-in for Windows' tar.exe, which Wine doesn't ship.
// Supports exactly: tar.exe -x -f <zip> -C <dir>, for zips whose entries are
// STORED (uncompressed) -- the rig builds its test packages that way.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

static unsigned rd16(const unsigned char* p) { return p[0] | (p[1] << 8); }
static unsigned rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

// MOCK_TAR_LINGER_MS: after unpacking, leave a helper process behind that
// sits in the output folder with it open (no delete sharing) for that long --
// what a virus scanner, or a handle that outlives tar's exit, looks like.
static void startLinger(const std::wstring& dir) {
    wchar_t ms[32];
    if (!GetEnvironmentVariableW(L"MOCK_TAR_LINGER_MS", ms, 32)) return;
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(self) + L"\" --linger " + ms;
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(self, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && !wcscmp(argv[1], L"--linger")) {
        wchar_t cwd[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, cwd);
        HANDLE h = CreateFileW(cwd, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        Sleep(_wtoi(argv[2]));
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        return 0;
    }
    std::wstring zip, dir;
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"-f") && i + 1 < argc) zip = argv[++i];
        else if (!wcscmp(argv[i], L"-C") && i + 1 < argc) dir = argv[++i];
    }
    if (zip.empty() || dir.empty()) return 2;
    FILE* f = _wfopen(zip.c_str(), L"rb");
    if (!f) return 2;
    std::vector<unsigned char> data;
    unsigned char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
    fclose(f);
    size_t pos = 0;
    while (pos + 30 <= data.size() && rd32(&data[pos]) == 0x04034b50) {
        const unsigned char* h = &data[pos];
        unsigned method = rd16(h + 8), csize = rd32(h + 18), nlen = rd16(h + 26), xlen = rd16(h + 28);
        if (method != 0 || (rd16(h + 6) & 8)) return 3;
        std::string name((const char*)h + 30, nlen);
        size_t body = pos + 30 + nlen + xlen;
        if (body + csize > data.size()) return 4;
        std::wstring wname;
        for (char c : name) wname += (c == '/') ? L'\\' : (wchar_t)(unsigned char)c;
        std::wstring path = dir + L"\\" + wname;
        // make parent folders
        for (size_t i = dir.size() + 1; i < path.size(); i++)
            if (path[i] == L'\\') CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
        if (!name.empty() && name.back() == '/') {
            CreateDirectoryW(path.c_str(), nullptr);
        } else {
            FILE* o = _wfopen(path.c_str(), L"wb");
            if (!o) return 5;
            fwrite(&data[body], 1, csize, o);
            // MOCK_TAR_PAD=<name>: that file comes out 100 bytes longer than
            // the zip says (an extractor that disagrees with the directory).
            wchar_t pad[260];
            if (GetEnvironmentVariableW(L"MOCK_TAR_PAD", pad, 260)) {
                std::wstring want = pad;
                if (wname.size() >= want.size() && wname.compare(wname.size() - want.size(), want.size(), want) == 0) {
                    static const char extra[100] = {};
                    fwrite(extra, 1, sizeof(extra), o);
                }
            }
            fclose(o);
        }
        pos = body + csize;
    }
    startLinger(dir);
    return 0;
}
