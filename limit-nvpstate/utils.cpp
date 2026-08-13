#include <utils.h>
#include <log.h>

#include <Shlwapi.h>
#include <vector>

namespace {

// The original hardcoded SOFTWARE\Wow6432Node\... which is the 32-bit redirected
// view. Addressing Wow6432Node directly is explicitly discouraged; use the real
// path and select the view with KEY_WOW64_64KEY.
constexpr wchar_t KEY_PATH_RUN[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t KEY_NAME_RUN[] = L"limit-nvpstate";

HKEY openRunKey(REGSAM access) {
    HKEY hKey = nullptr;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, KEY_PATH_RUN, 0, access | KEY_WOW64_64KEY, &hKey) != ERROR_SUCCESS) {
        LOG(L"error: could not open the Run key (%lu)\n", GetLastError());
        return nullptr;
    }

    return hKey;
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
    HKEY hKey = openRunKey(KEY_QUERY_VALUE);

    if (!hKey) {
        return false;
    }

    const LSTATUS result = RegQueryValueExW(hKey, KEY_NAME_RUN, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(hKey);

    return result == ERROR_SUCCESS;
}

bool addToStartup(bool isEnabled) {
    HKEY hKey = openRunKey(KEY_SET_VALUE);

    if (!hKey) {
        return false;
    }

    LSTATUS result;

    if (isEnabled) {
        // Quote the path. An unquoted value containing spaces (anything under
        // "C:\Program Files\...") is parsed incorrectly the moment an argument
        // is appended, and is a classic unquoted-service-path style problem.
        const std::wstring value = L"\"" + getProgramPath() + L"\"";

        result = RegSetValueExW(hKey,
                                KEY_NAME_RUN,
                                0,
                                REG_SZ,
                                reinterpret_cast<const BYTE*>(value.c_str()),
                                static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    } else {
        result = RegDeleteValueW(hKey, KEY_NAME_RUN);

        if (result == ERROR_FILE_NOT_FOUND) {
            result = ERROR_SUCCESS;
        }
    }

    RegCloseKey(hKey);

    return result == ERROR_SUCCESS;
}

bool getProcessNameByPID(DWORD pid, wchar_t* out, size_t cch) {
    if (!out || cch == 0) {
        return false;
    }

    out[0] = L'\0';

    if (pid == 0) {
        return false;
    }

    // PROCESS_QUERY_LIMITED_INFORMATION succeeds against protected and elevated
    // processes where PROCESS_QUERY_INFORMATION | PROCESS_VM_READ fails, and
    // QueryFullProcessImageNameW does not need the module list to be readable.
    //
    // Between them this removes the CreateToolhelp32Snapshot fallback the
    // original needed -- a full system process snapshot that could fire on an
    // ordinary alt-tab.
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

    if (!hProcess) {
        return false;
    }

    wchar_t fullPath[MAX_PATH];
    DWORD   size = MAX_PATH;

    if (QueryFullProcessImageNameW(hProcess, 0, fullPath, &size)) {
        CloseHandle(hProcess);
        return wcscpy_s(out, cch, PathFindFileNameW(fullPath)) == 0;
    }

    // Rare: paths longer than MAX_PATH.
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        std::vector<wchar_t> buffer(32768);
        size = static_cast<DWORD>(buffer.size());

        if (QueryFullProcessImageNameW(hProcess, 0, buffer.data(), &size)) {
            CloseHandle(hProcess);
            return wcscpy_s(out, cch, PathFindFileNameW(buffer.data())) == 0;
        }
    }

    CloseHandle(hProcess);
    return false;
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
