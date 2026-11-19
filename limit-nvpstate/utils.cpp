#include <utils.h>
#include <log.h>

#include <Shlwapi.h>
#include <tlhelp32.h>
#include <vector>

namespace {

// Use the real path and pick the registry view with KEY_WOW64_*KEY instead of
// spelling out Wow6432Node.
constexpr wchar_t KEY_PATH_RUN[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t KEY_NAME_RUN[] = L"limit-nvpstate";

// 5.2.1 and earlier wrote the startup entry under Wow6432Node, i.e. the 32-bit
// view. Windows still runs entries from both views at logon, so an upgraded
// install has to look in, and clean up, both. Otherwise the menu shows
// "unticked" for an entry that is still there and that unticking cannot remove.
constexpr REGSAM VIEW_CURRENT = KEY_WOW64_64KEY;
constexpr REGSAM VIEW_LEGACY  = KEY_WOW64_32KEY;

// Returns the RegOpenKeyExW status so callers can tell "key missing" (the
// 32-bit view may not exist at all) from a real failure.
LSTATUS openRunKey(REGSAM access, REGSAM view, HKEY* hKey) {
    *hKey = nullptr;

    const LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, KEY_PATH_RUN, 0, access | view, hKey);

    if (status != ERROR_SUCCESS) {
        LOG(L"warn: could not open the Run key (view %lx, status %ld)\n",
            static_cast<unsigned long>(view), static_cast<long>(status));
    }

    return status;
}

bool runValueExists(REGSAM view) {
    HKEY hKey = nullptr;

    if (openRunKey(KEY_QUERY_VALUE, view, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    const LSTATUS result = RegQueryValueExW(hKey, KEY_NAME_RUN, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(hKey);

    return result == ERROR_SUCCESS;
}

// Missing value counts as success: the goal is "not there".
bool deleteRunValue(REGSAM view) {
    HKEY          hKey   = nullptr;
    const LSTATUS opened = openRunKey(KEY_SET_VALUE, view, &hKey);

    if (opened == ERROR_FILE_NOT_FOUND) {
        return true;   // no key, so certainly no value
    }

    if (opened != ERROR_SUCCESS) {
        return false;
    }

    LSTATUS result = RegDeleteValueW(hKey, KEY_NAME_RUN);
    RegCloseKey(hKey);

    if (result == ERROR_FILE_NOT_FOUND) {
        result = ERROR_SUCCESS;
    }

    return result == ERROR_SUCCESS;
}

// Fallback for processes that refuse OpenProcess, which anti-cheat drivers can
// do for a game's own process. A full snapshot is slow, so it only runs when
// the fast path has already failed.
bool getProcessNameFromSnapshot(DWORD pid, wchar_t* out, size_t cch) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize          = sizeof(entry);

    bool found = false;

    if (Process32FirstW(hSnapshot, &entry)) {
        do {
            if (entry.th32ProcessID == pid) {
                found = wcscpy_s(out, cch, entry.szExeFile) == 0;
                break;
            }
        } while (Process32NextW(hSnapshot, &entry));
    }

    CloseHandle(hSnapshot);
    return found;
}

bool getProcessNameFromHandle(DWORD pid, wchar_t* out, size_t cch) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

    if (!hProcess) {
        return false;
    }

    bool ok = false;

    wchar_t fullPath[MAX_PATH];
    DWORD   size = MAX_PATH;

    if (QueryFullProcessImageNameW(hProcess, 0, fullPath, &size)) {
        ok = wcscpy_s(out, cch, PathFindFileNameW(fullPath)) == 0;
    } else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        // Rare: paths longer than MAX_PATH.
        std::vector<wchar_t> buffer(32768);
        size = static_cast<DWORD>(buffer.size());

        if (QueryFullProcessImageNameW(hProcess, 0, buffer.data(), &size)) {
            ok = wcscpy_s(out, cch, PathFindFileNameW(buffer.data())) == 0;
        }
    }

    CloseHandle(hProcess);
    return ok;
}

} // namespace

std::wstring getProgramPath() {
    std::wstring path(MAX_PATH, L'\0');

    for (;;) {
        const DWORD written = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));

        if (written == 0) {
            return std::wstring();
        }

        if (written < path.size()) {
            path.resize(written);
            return path;
        }

        if (path.size() >= 32768) {
            return std::wstring();
        }

        path.resize(path.size() * 2);
    }
}

std::wstring getProgramDirectory() {
    std::wstring path = getProgramPath();

    const size_t separator = path.find_last_of(L"\\/");

    if (separator == std::wstring::npos) {
        return std::wstring();
    }

    path.resize(separator);
    return path;
}

const wchar_t* getBaseName(const wchar_t* fullPath) {
    return fullPath ? PathFindFileNameW(fullPath) : L"";
}

bool isAddedToStartup() {
    return runValueExists(VIEW_CURRENT) || runValueExists(VIEW_LEGACY);
}

bool addToStartup(bool isEnabled) {
    if (!isEnabled) {
        // Both, unconditionally: && would skip the legacy view on failure.
        const bool removedCurrent = deleteRunValue(VIEW_CURRENT);
        const bool removedLegacy  = deleteRunValue(VIEW_LEGACY);
        return removedCurrent && removedLegacy;
    }

    HKEY hKey = nullptr;

    if (openRunKey(KEY_SET_VALUE, VIEW_CURRENT, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    // Quote the path. An unquoted value containing spaces (anything under
    // "C:\Program Files\...") is parsed incorrectly the moment an argument
    // is appended.
    const std::wstring value = L"\"" + getProgramPath() + L"\"";

    const LSTATUS result = RegSetValueExW(hKey,
                                          KEY_NAME_RUN,
                                          0,
                                          REG_SZ,
                                          reinterpret_cast<const BYTE*>(value.c_str()),
                                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS) {
        return false;
    }

    // Migrate: one entry, in the current view. A leftover legacy entry would
    // start a second copy at every logon.
    if (!deleteRunValue(VIEW_LEGACY)) {
        LOG(L"warn: could not remove the legacy startup entry\n");
    }

    return true;
}

bool getProcessNameByPID(DWORD pid, wchar_t* out, size_t cch) {
    if (!out || cch == 0) {
        return false;
    }

    out[0] = L'\0';

    if (pid == 0) {
        return false;
    }

    // PROCESS_QUERY_LIMITED_INFORMATION works against most protected and
    // elevated processes, and QueryFullProcessImageNameW does not need the
    // module list to be readable. When even that is refused, fall back to a
    // snapshot like the original did.
    return getProcessNameFromHandle(pid, out, cch) || getProcessNameFromSnapshot(pid, out, cch);
}

std::string wideToUtf8(const wchar_t* str) {
    if (!str || !*str) {
        return std::string();
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, str, -1, nullptr, 0, nullptr, nullptr);

    if (size <= 1) {
        return std::string();
    }

    std::string out(static_cast<size_t>(size) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, str, -1, out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring utf8ToWide(const char* str) {
    if (!str || !*str) {
        return std::wstring();
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);

    if (size <= 1) {
        return std::wstring();
    }

    std::wstring out(static_cast<size_t>(size) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str, -1, out.data(), size);
    return out;
}
