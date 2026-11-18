#include <updater.h>

#include <log.h>
#include <testhooks.h>
#include <updaterules.h>
#include <version.h>

#include <nlohmann/json.hpp>

#include <Windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>

namespace updater {

namespace {

const wchar_t RELEASES_PAGE[] = L"https://github.com/NicholasBly/limit-nvpstate/releases";
const wchar_t LATEST_API[]    = L"https://api.github.com/repos/NicholasBly/limit-nvpstate/releases/latest";

constexpr long long MAX_API_BYTES      = 1LL << 20;     // 1 MiB of JSON
constexpr long long MAX_CHECKSUM_BYTES = 4096;
constexpr long long MAX_ZIP_BYTES      = 200LL << 20;   // 200 MiB
constexpr DWORD     TAR_TIMEOUT_MS     = 120000;

// Release builds talk to GitHub over HTTPS only. Test builds (the Wine rig)
// may point at a local plain-HTTP server through an environment variable.
#ifdef LIMITNV_UPDATE_TEST
bool         allowHttp() { return true; }
std::wstring latestApiUrl() {
    wchar_t buffer[1024];
    const DWORD n = GetEnvironmentVariableW(L"LIMITNV_UPDATE_API", buffer, 1024);
    return (n > 0 && n < 1024) ? std::wstring(buffer, n) : std::wstring(LATEST_API);
}
#else
bool         allowHttp() { return false; }
std::wstring latestApiUrl() { return LATEST_API; }
#endif

std::wstring widen(const std::string& s) {
    if (s.empty()) {
        return {};
    }

    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);

    if (n <= 0) {
        return {};
    }

    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string narrow(const std::wstring& s) {
    if (s.empty()) {
        return {};
    }

    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);

    if (n <= 0) {
        return {};
    }

    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring errorText(const wchar_t* what, DWORD code) {
    return std::wstring(what) + L" (error " + std::to_wstring(code) + L")";
}

class Inet {
public:
    explicit Inet(HINTERNET h = nullptr) : h(h) {}
    ~Inet() { if (h) WinHttpCloseHandle(h); }
    Inet(const Inet&)            = delete;
    Inet& operator=(const Inet&) = delete;
    operator HINTERNET() const { return h; }
    explicit operator bool() const { return h != nullptr; }

private:
    HINTERNET h;
};

bool isHttpsUrl(const std::wstring& url) {
    return url.rfind(L"https://", 0) == 0 || (allowHttp() && url.rfind(L"http://", 0) == 0);
}

// One GET. Each received chunk goes to `sink`; GitHub's asset links redirect
// to another HTTPS host, which WinHTTP follows (never HTTPS -> HTTP).
bool httpGet(const std::wstring&                             url,
             const std::function<bool(const char*, DWORD)>& sink,
             long long                                       maxBytes,
             std::wstring&                                   error,
             const Progress&                                 progress) {
    if (!isHttpsUrl(url)) {
        error = L"Refusing a download that is not HTTPS: " + url;
        return false;
    }

    std::vector<wchar_t> host(256), path(4096), extra(4096);

    URL_COMPONENTS parts    = {};
    parts.dwStructSize      = sizeof(parts);
    parts.lpszHostName      = host.data();
    parts.dwHostNameLength  = static_cast<DWORD>(host.size());
    parts.lpszUrlPath       = path.data();
    parts.dwUrlPathLength   = static_cast<DWORD>(path.size());
    parts.lpszExtraInfo     = extra.data();
    parts.dwExtraInfoLength = static_cast<DWORD>(extra.size());

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        error = errorText(L"Bad URL", GetLastError());
        return false;
    }

    const bool         https  = parts.nScheme == INTERNET_SCHEME_HTTPS;
    const std::wstring object = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength)
                              + std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const std::wstring agent  = L"limit-nvpstate/" + widen(APP_VERSION_STRING);

    HINTERNET rawSession = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0);

    if (!rawSession) {   // AUTOMATIC_PROXY needs Windows 8.1
        rawSession = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0);
    }

    Inet session(rawSession);

    if (!session) {
        error = errorText(L"Could not start WinHTTP", GetLastError());
        return false;
    }

    if (!WinHttpSetTimeouts(session, 15000, 15000, 15000, 30000)) {
        error = errorText(L"Could not set network timeouts", GetLastError());
        return false;
    }

    Inet connection(WinHttpConnect(session, std::wstring(parts.lpszHostName, parts.dwHostNameLength).c_str(),
                                   parts.nPort, 0));
    Inet request(connection ? WinHttpOpenRequest(connection, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0)
                            : nullptr);

    if (!request) {
        error = errorText(L"Could not connect", GetLastError());
        return false;
    }

    WinHttpAddRequestHeaders(request, L"Accept: application/vnd.github+json, */*", static_cast<DWORD>(-1),
                             WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(request, nullptr)) {
        error = errorText(L"Could not reach the update server", GetLastError());
        return false;
    }

    DWORD status = 0;
    DWORD size   = sizeof(status);

    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        error = errorText(L"The update server sent an unreadable answer", GetLastError());
        return false;
    }

    if (status != 200) {
        error = L"The server answered HTTP " + std::to_wstring(status);
        return false;
    }

    long long total  = -1;
    DWORD     length = 0;
    size             = sizeof(length);

    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX)) {
        total = length;
    }

    if (total > maxBytes) {
        error = L"The download is unexpectedly large";
        return false;
    }

    std::vector<char> buffer(64 * 1024);
    long long         received = 0;

    for (;;) {
        DWORD available = 0;

        if (!WinHttpQueryDataAvailable(request, &available)) {
            error = errorText(L"The download broke off", GetLastError());
            return false;
        }

        if (available == 0) {
            break;
        }

        DWORD got = 0;

        if (!WinHttpReadData(request, buffer.data(),
                             available < buffer.size() ? available : static_cast<DWORD>(buffer.size()), &got)) {
            error = errorText(L"The download broke off", GetLastError());
            return false;
        }

        if (got == 0) {
            break;
        }

        received += got;

        if (received > maxBytes) {
            error = L"The download is unexpectedly large";
            return false;
        }

        if (!sink(buffer.data(), got)) {
            error = errorText(L"Could not save the download", GetLastError());
            return false;
        }

        if (progress && !progress(received, total)) {
            error = L"Cancelled";
            return false;
        }
    }

    if (total >= 0 && received != total) {
        error = L"The download is incomplete";
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

bool fileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool directoryExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool isReparsePoint(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

// Small text files of the updater's own (journal, markers): UTF-16, flushed
// to disk before returning, so they survive a power cut right after.
bool writeTextFile(const std::wstring& path, const std::wstring& text, DWORD disposition) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    const std::wstring content = L"\xFEFF" + text;
    const DWORD        bytes   = static_cast<DWORD>(content.size() * sizeof(wchar_t));
    DWORD              written = 0;
    const bool         ok      = WriteFile(file, content.data(), bytes, &written, nullptr) && written == bytes
                      && FlushFileBuffers(file);

    CloseHandle(file);

    if (!ok) {
        DeleteFileW(path.c_str());
    }

    return ok;
}

bool readTextFile(const std::wstring& path, std::wstring& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size = {};
    bool          ok   = GetFileSizeEx(file, &size) && size.QuadPart >= 2 && size.QuadPart <= (4 << 20)
             && size.QuadPart % 2 == 0;

    if (ok) {
        std::wstring buffer(static_cast<size_t>(size.QuadPart / 2), L'\0');
        DWORD        read = 0;

        ok = ReadFile(file, buffer.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr)
          && read == static_cast<DWORD>(size.QuadPart) && buffer[0] == L'\xFEFF';

        if (ok) {
            text = buffer.substr(1);
        }
    }

    CloseHandle(file);
    return ok;
}

bool sha256File(const std::wstring& path, std::string& hexOut, std::wstring& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        error = errorText(L"Could not open the download to check it", GetLastError());
        return false;
    }

    BCRYPT_ALG_HANDLE  algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash      = nullptr;
    LARGE_INTEGER      size      = {};
    NTSTATUS           status    = 0;
    bool               ok        = false;

    if (!GetFileSizeEx(file, &size)) {
        error = errorText(L"Could not read the download", GetLastError());
    } else if ((status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) != 0
               || (status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0)) != 0) {
        error = L"Could not start SHA-256 (status " + std::to_wstring(status) + L")";
    } else {
        std::vector<unsigned char> buffer(64 * 1024);
        long long                  total = 0;

        ok = true;

        for (;;) {
            DWORD read     = 0;
            BOOL  readOk   = ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr);

            if (readOk && total > 0 && testFails(L"LIMITNV_TEST_FAIL_HASH_READ", L"1")) {
                readOk = FALSE;
                SetLastError(ERROR_READ_FAULT);
            }

            // A failed read is an error, not the end of the file: hashing
            // only the part that was read would be meaningless.
            if (!readOk) {
                error = errorText(L"Could not read the download to check it", GetLastError());
                ok    = false;
                break;
            }

            if (read == 0) {
                break;
            }

            if ((status = BCryptHashData(hash, buffer.data(), read, 0)) != 0) {
                error = L"SHA-256 failed (status " + std::to_wstring(status) + L")";
                ok    = false;
                break;
            }

            total += read;
        }

        if (ok && total != size.QuadPart) {
            error = L"The download changed while it was being checked";
            ok    = false;
        }

        unsigned char digest[32];

        if (ok && (status = BCryptFinishHash(hash, digest, sizeof(digest), 0)) != 0) {
            error = L"SHA-256 failed (status " + std::to_wstring(status) + L")";
            ok    = false;
        }

        if (ok) {
            static const char hex[] = "0123456789abcdef";
            hexOut.clear();

            for (unsigned char b : digest) {
                hexOut += hex[b >> 4];
                hexOut += hex[b & 15];
            }
        }
    }

    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return ok;
}

// Every file under root\relative (as paths relative to root) and their total
// size. Strict: a link anywhere, or any error while listing, fails the whole
// thing -- a partial list must never be installed as if it were complete.
bool listPackage(const std::wstring&        root,
                 const std::wstring&        relative,
                 std::vector<std::wstring>& out,
                 unsigned long long&        bytes,
                 int                        depth,
                 std::wstring&              error) {
    if (depth > 16) {
        error = L"The update package is nested too deeply";
        return false;
    }

    WIN32_FIND_DATAW   data;
    const std::wstring pattern = root + L"\\" + (relative.empty() ? L"" : relative + L"\\") + L"*";
    HANDLE             find    = FindFirstFileW(pattern.c_str(), &data);

    if (find == INVALID_HANDLE_VALUE) {
        error = errorText(L"Could not read the update package", GetLastError());
        return false;
    }

    bool ok = true;

    do {
        const std::wstring name = data.cFileName;

        if (name == L"." || name == L"..") {
            continue;
        }

        const std::wstring child = relative.empty() ? name : relative + L"\\" + name;

        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            error = L"The update package contains a link, so it was not installed: " + child;
            ok    = false;
            break;
        }

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!listPackage(root, child, out, bytes, depth + 1, error)) {
                ok = false;
                break;
            }
        } else {
            out.push_back(child);
            bytes += (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;

            if (out.size() > 5000) {
                error = L"The update package has too many files";
                ok    = false;
                break;
            }
        }
    } while (FindNextFileW(find, &data));

    // Read straight after FindNextFileW said no: anything but "no more files"
    // means the list is incomplete.
    if (ok && GetLastError() != ERROR_NO_MORE_FILES) {
        error = errorText(L"Could not read the update package", GetLastError());
        ok    = false;
    }

    FindClose(find);
    return ok;
}

// One pass. Links (junctions, symlinks) are removed themselves, never
// followed. True if `dir` is gone afterwards.
bool removeTreeOnce(const std::wstring& dir) {
    WIN32_FIND_DATAW data;
    HANDLE           find = FindFirstFileW((dir + L"\\*").c_str(), &data);

    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = data.cFileName;

            if (name == L"." || name == L"..") {
                continue;
            }

            const std::wstring child = dir + L"\\" + name;

            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                    RemoveDirectoryW(child.c_str());
                } else {
                    removeTreeOnce(child);
                }
            } else {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
                    SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                }

                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(find, &data));

        FindClose(find);
    }

    RemoveDirectoryW(dir.c_str());
    return GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES;
}

bool makeDirectories(const std::wstring& base, const std::wstring& relative) {
    size_t slash = relative.find(L'\\');

    while (slash != std::wstring::npos) {
        const std::wstring dir = base + L"\\" + relative.substr(0, slash);

        if (!directoryExists(dir) && !CreateDirectoryW(dir.c_str(), nullptr)) {
            return false;
        }

        slash = relative.find(L'\\', slash + 1);
    }

    return true;
}

bool runTar(const std::wstring& zipPath, const std::wstring& outDir, std::wstring& error) {
    wchar_t    system[MAX_PATH];
    const UINT n = GetSystemDirectoryW(system, MAX_PATH);

    if (n == 0 || n >= MAX_PATH) {
        error = L"Could not find the Windows folder";
        return false;
    }

    const std::wstring tar = std::wstring(system) + L"\\tar.exe";

    if (!fileExists(tar)) {
        error = L"This version of Windows has no tar.exe (it ships with Windows 10 1803 and newer), so the "
                L"update can't be unpacked automatically";
        return false;
    }

    std::wstring command = L"\"" + tar + L"\" -x -f \"" + zipPath + L"\" -C \"" + outDir + L"\"";

    STARTUPINFOW        startup = {};
    PROCESS_INFORMATION process = {};
    startup.cb                  = sizeof(startup);

    if (!CreateProcessW(tar.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        outDir.c_str(), &startup, &process)) {
        error = errorText(L"Could not run tar.exe", GetLastError());
        return false;
    }

    const DWORD waited = WaitForSingleObject(process.hProcess, TAR_TIMEOUT_MS);
    DWORD       code   = 1;

    if (waited == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &code);
    } else {
        // Stopped, and waited for, before anyone deletes what it was writing.
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 10000);
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    if (waited != WAIT_OBJECT_0) {
        error = L"Unpacking the update took too long";
        return false;
    }

    if (code != 0) {
        error = L"Unpacking the update failed (tar.exe exit code " + std::to_wstring(code) + L")";
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// The work folder
// ---------------------------------------------------------------------------

const wchar_t WORK_DIR_NAME[] = L"limit-nvpstate-update";
const wchar_t OWNER_FILE[]    = L"owner.txt";
const wchar_t JOURNAL_FILE[]  = L"journal.txt";
const wchar_t INSTALLED[]     = L"installed";
const wchar_t CONFIRMED[]     = L"confirmed";

std::wstring workDir(const std::wstring& appDir) {
    return appDir + L"\\" + WORK_DIR_NAME;
}

std::wstring workFile(const std::wstring& appDir, const wchar_t* name) {
    return workDir(appDir) + L"\\" + name;
}

// Ours only with the marker inside, and never a link to somewhere else.
bool ownsWorkDir(const std::wstring& appDir) {
    const std::wstring dir = workDir(appDir);
    return directoryExists(dir) && !isReparsePoint(dir) && fileExists(workFile(appDir, OWNER_FILE));
}

// A fresh, empty work folder for a new download.
bool openWorkDir(const std::wstring& appDir, std::wstring& error) {
    const std::wstring dir = workDir(appDir);

    if (GetFileAttributesW(dir.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (!ownsWorkDir(appDir)) {
            error = L"Something named \"limit-nvpstate-update\" in the program folder is in the way. It isn't "
                    L"the updater's, so it was left alone. Move or rename it to update";
            return false;
        }

        if (fileExists(workFile(appDir, JOURNAL_FILE))) {
            error = L"An earlier update has not finished. Restart limit-nvpstate, then try again";
            return false;
        }

        if (!removeTree(dir)) {
            error = L"Could not clear the updater's folder (" + dir + L")";
            return false;
        }
    }

    // Inherits the program folder's permissions: no easier to change than
    // the exe next to it.
    if (!CreateDirectoryW(dir.c_str(), nullptr)) {
        error = errorText(L"Could not create the updater's folder", GetLastError());
        return false;
    }

    if (isReparsePoint(dir) || !writeTextFile(workFile(appDir, OWNER_FILE), L"limit-nvpstate updater\n", CREATE_NEW)
        || !CreateDirectoryW((dir + L"\\download").c_str(), nullptr)) {
        error = errorText(L"Could not set up the updater's folder", GetLastError());
        return false;
    }

    return true;
}

// Moves a file out of the way: deleted if possible, otherwise renamed into
// the work folder's trash (a running exe can be renamed but not deleted).
bool takeOut(const std::wstring& appDir, const std::wstring& path, int& trashCount) {
    if (DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) {
        return true;
    }

    const std::wstring trash = workDir(appDir) + L"\\trash";
    CreateDirectoryW(trash.c_str(), nullptr);

    return MoveFileExW(path.c_str(), (trash + L"\\" + std::to_wstring(++trashCount)).c_str(), MOVEFILE_WRITE_THROUGH)
        != FALSE;
}

} // namespace

const wchar_t* releasesPageUrl() {
    return RELEASES_PAGE;
}

std::wstring backupDir(const std::wstring& appDir) {
    return workDir(appDir) + L"\\backup";
}

bool removeTree(const std::wstring& dir) {
    if (dir.empty() || GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return true;
    }

    if (isReparsePoint(dir)) {
        return RemoveDirectoryW(dir.c_str()) != FALSE;   // the link, not what it points at
    }

    // Something can hold a file or folder open for a moment after we are done
    // with it -- tar.exe's working folder as it exits, a virus scanner looking
    // at the fresh files -- and deleting then fails with a sharing violation.
    // So retry with short waits (about 3 s at most).
    for (int pass = 0; pass < 10; pass++) {
        if (pass > 0) {
            Sleep(pass < 3 ? 100u << (pass - 1) : 400u);
        }

        if (removeTreeOnce(dir)) {
            return true;
        }
    }

    return false;
}

bool fetchLatest(Release& out, std::wstring& error) {
    std::string body;

    if (!httpGet(latestApiUrl(), [&](const char* data, DWORD n) { body.append(data, n); return true; },
                 MAX_API_BYTES, error, nullptr)) {
        return false;
    }

    const nlohmann::json reply = nlohmann::json::parse(body, nullptr, false);

    const auto tag   = reply.is_object() ? reply.find("tag_name") : reply.end();
    const auto page  = reply.is_object() ? reply.find("html_url") : reply.end();
    const auto files = reply.is_object() ? reply.find("assets") : reply.end();

    if (!reply.is_object() || tag == reply.end() || !tag->is_string() || files == reply.end() || !files->is_array()) {
        error = L"Unexpected reply from GitHub";
        return false;
    }

    out         = Release{};
    out.version = tag->get<std::string>();
    out.pageUrl = (page != reply.end() && page->is_string()) ? widen(page->get<std::string>()) : RELEASES_PAGE;

    if (!updaterules::isReleaseTag(out.version)) {
        error = L"The latest release has a version this program doesn't understand (" + widen(out.version) + L")";
        return false;
    }

    std::vector<std::string> names;
    std::vector<std::string> urls;

    for (const auto& asset : *files) {
        const auto name = asset.is_object() ? asset.find("name") : asset.end();
        const auto url  = asset.is_object() ? asset.find("browser_download_url") : asset.end();

        if (asset.is_object() && name != asset.end() && url != asset.end() && name->is_string() && url->is_string()) {
            names.push_back(name->get<std::string>());
            urls.push_back(url->get<std::string>());
        }
    }

    const updaterules::AssetChoice choice = updaterules::chooseAssets(names, out.version);

    if (choice.zip < 0) {
        error = L"The latest release has no limit-nvpstate-" + widen(out.version) + L".zip";
        return false;
    }

    out.zipName = widen(names[static_cast<size_t>(choice.zip)]);
    out.zipUrl  = widen(urls[static_cast<size_t>(choice.zip)]);

    if (choice.checksum >= 0) {
        out.checksumUrl = widen(urls[static_cast<size_t>(choice.checksum)]);
    }

    // The zip name becomes a file name in our folder.
    if (!updaterules::isSafeRelativePath(out.zipName) || out.zipName.find_first_of(L"\\/") != std::wstring::npos) {
        error = L"The release zip has an unexpected name";
        return false;
    }

    return true;
}

bool prepare(const Release&      release,
             const std::wstring& appDir,
             std::wstring&       packageDir,
             std::wstring&       error,
             const Progress&     progress) {
    // Asked between every step; the download itself asks after every chunk.
    const auto cancelled = [&]() {
        if (progress && !progress(-1, -1)) {
            error = L"Cancelled";
            return true;
        }

        return false;
    };

    if (release.checksumUrl.empty()) {
        error = L"This release has no checksum file, so it can't be verified";
        return false;
    }

    if (cancelled() || !openWorkDir(appDir, error)) {
        return false;
    }

    const std::wstring download = workDir(appDir) + L"\\download";
    const Progress     onlyCancel = [&](long long, long long) { return !progress || progress(-1, -1); };

    // ---- checksum first: small, and no point downloading 20 MB without it
    std::string checksumText;

    if (!httpGet(release.checksumUrl, [&](const char* data, DWORD got) { checksumText.append(data, got); return true; },
                 MAX_CHECKSUM_BYTES, error, onlyCancel)) {
        if (error != L"Cancelled") error = L"Could not download the checksum: " + error;
        return false;
    }

    std::string expected;

    if (!updaterules::parseChecksumFile(checksumText, narrow(release.zipName), expected)) {
        error = L"The release's checksum file is not readable";
        return false;
    }

    if (cancelled()) {
        return false;
    }

    // ---- the zip
    const std::wstring zipPath = download + L"\\" + release.zipName;

    HANDLE file = CreateFileW(zipPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        error = errorText(L"Could not create the download file", GetLastError());
        return false;
    }

    const bool downloaded = httpGet(release.zipUrl, [&](const char* data, DWORD got) {
        DWORD written = 0;
        return WriteFile(file, data, got, &written, nullptr) && written == got;
    }, MAX_ZIP_BYTES, error, progress);

    CloseHandle(file);

    if (!downloaded) {
        if (error != L"Cancelled") error = L"Could not download the update: " + error;
        return false;
    }

    testPause(L"LIMITNV_TEST_PAUSE_AFTER_DOWNLOAD");

    if (cancelled()) {
        return false;
    }

    std::string actual;

    if (!sha256File(zipPath, actual, error)) {
        return false;
    }

    if (actual != expected) {
        error = L"The download does not match the release's checksum, so it was not installed";
        return false;
    }

    if (cancelled()) {
        return false;
    }

    // ---- unpack, then find the one folder holding the program
    const std::wstring unpacked = download + L"\\unpacked";

    if (!CreateDirectoryW(unpacked.c_str(), nullptr)) {
        error = errorText(L"Could not create the unpack folder", GetLastError());
        return false;
    }

    if (!runTar(zipPath, unpacked, error) || cancelled()) {
        return false;
    }

    std::wstring root;

    if (fileExists(unpacked + L"\\limit-nvpstate.exe")) {
        root = unpacked;
    } else {
        WIN32_FIND_DATAW data;
        HANDLE           find = FindFirstFileW((unpacked + L"\\*").c_str(), &data);

        if (find != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring name = data.cFileName;

                if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && name != L"." && name != L".."
                    && fileExists(unpacked + L"\\" + name + L"\\limit-nvpstate.exe")) {
                    if (!root.empty() || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                        FindClose(find);
                        error = L"The update package's layout is not the expected one";
                        return false;
                    }

                    root = unpacked + L"\\" + name;
                }
            } while (FindNextFileW(find, &data));

            FindClose(find);
        }
    }

    if (root.empty()) {
        error = L"The update package does not contain limit-nvpstate.exe";
        return false;
    }

    // ---- check the whole package before anything is installed
    std::vector<std::wstring> files;
    unsigned long long        bytes = 0;

    if (!listPackage(root, L"", files, bytes, 0, error)) {
        return false;
    }

    error = updaterules::packageProblem(files, bytes);

    if (!error.empty()) {
        return false;
    }

    packageDir = root;
    return true;
}

void discardDownload(const std::wstring& appDir) {
    if (ownsWorkDir(appDir) && !fileExists(workFile(appDir, JOURNAL_FILE))) {
        removeTree(workDir(appDir));
    }
}

InstallResult install(const std::wstring& packageDir, const std::wstring& appDir, std::wstring& error) {
    const std::wstring work = workDir(appDir);

    // ---- check and plan; nothing is touched yet
    if (!ownsWorkDir(appDir) || packageDir.compare(0, work.size() + 1, work + L"\\") != 0) {
        error = L"The update package is not in the updater's folder";
        return InstallResult::RolledBack;
    }

    std::vector<std::wstring> files;
    unsigned long long        bytes = 0;

    if (!listPackage(packageDir, L"", files, bytes, 0, error)) {
        return InstallResult::RolledBack;
    }

    error = updaterules::packageProblem(files, bytes);

    if (!error.empty()) {
        return InstallResult::RolledBack;
    }

    // A fixed order (not whatever order the file system lists them in), with
    // the exe last: until it is swapped, a crash leaves the old program in
    // place, which then undoes the rest at its next start.
    std::sort(files.begin(), files.end(), [](const std::wstring& a, const std::wstring& b) {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });

    std::stable_partition(files.begin(), files.end(), [](const std::wstring& f) {
        return CompareStringOrdinal(f.c_str(), -1, L"limit-nvpstate.exe", -1, TRUE) != CSTR_EQUAL;
    });

    std::vector<updaterules::JournalEntry> plan;
    plan.push_back({ L'P', packageDir.substr(work.size() + 1) });

    std::vector<std::wstring> plannedDirs;

    for (const std::wstring& relative : files) {
        const std::wstring target = appDir + L"\\" + relative;

        // Your settings stay yours.
        if (CompareStringOrdinal(relative.c_str(), -1, L"config.json", -1, TRUE) == CSTR_EQUAL && fileExists(target)) {
            continue;
        }

        // Every folder on the way must be a real folder inside the program
        // folder -- not a link that leads somewhere else, not a file.
        for (size_t slash = relative.find(L'\\'); slash != std::wstring::npos; slash = relative.find(L'\\', slash + 1)) {
            const std::wstring folder     = relative.substr(0, slash);
            const DWORD        attributes = GetFileAttributesW((appDir + L"\\" + folder).c_str());

            if (attributes == INVALID_FILE_ATTRIBUTES) {
                bool planned = false;

                for (const std::wstring& d : plannedDirs) {
                    planned |= CompareStringOrdinal(d.c_str(), -1, folder.c_str(), -1, TRUE) == CSTR_EQUAL;
                }

                if (!planned) {
                    plannedDirs.push_back(folder);
                    plan.push_back({ L'D', folder });
                }
            } else if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                error = L"\"" + folder + L"\" in the program folder is not a normal folder, so the update was not "
                        L"installed";
                return InstallResult::RolledBack;
            }
        }

        const DWORD attributes = GetFileAttributesW(target.c_str());

        if (attributes == INVALID_FILE_ATTRIBUTES) {
            plan.push_back({ L'A', relative });
        } else if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            plan.push_back({ L'R', relative });
        } else {
            error = L"\"" + relative + L"\" in the program folder is in the way, so the update was not installed";
            return InstallResult::RolledBack;
        }
    }

    // ---- the plan reaches the disk before any program file moves
    if (!writeTextFile(workFile(appDir, JOURNAL_FILE), updaterules::formatJournal(plan), CREATE_NEW)) {
        error = errorText(L"Could not write the update journal", GetLastError());
        return InstallResult::RolledBack;
    }

    const auto fail = [&](const std::wstring& what) {
        error = errorText(what.c_str(), GetLastError());

        std::wstring undoError;

        if (undoInstall(appDir, undoError)) {
            return InstallResult::RolledBack;
        }

        error += L"\n\n" + undoError;
        return InstallResult::NeedsRepair;
    };

    unsigned stepNumber = 0;

    for (const updaterules::JournalEntry& step : plan) {
        const std::wstring target = appDir + L"\\" + step.path;
        const std::wstring source = packageDir + L"\\" + step.path;

        if (step.kind == L'P') {
            continue;
        }

        testPause(L"LIMITNV_TEST_PAUSE_INSTALL_STEP");

        if (++stepNumber == testValue(L"LIMITNV_TEST_FAIL_INSTALL_AT", 0)) {
            SetLastError(ERROR_SHARING_VIOLATION);
            return fail(L"Could not set aside " + step.path);
        }

        if (step.kind == L'D') {
            if (!CreateDirectoryW(target.c_str(), nullptr)) {
                return fail(L"Could not create the folder " + step.path);
            }
        } else if (step.kind == L'A') {
            if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
                return fail(L"Could not put " + step.path + L" in place");
            }
        } else {
            // Same volume (the work folder is inside the program folder), so
            // these are plain renames -- allowed even for the running exe.
            if ((!directoryExists(backupDir(appDir)) && !CreateDirectoryW(backupDir(appDir).c_str(), nullptr))
                || !makeDirectories(backupDir(appDir), step.path)) {
                return fail(L"Could not create the backup folder for " + step.path);
            }

            if (!MoveFileExW(target.c_str(), (backupDir(appDir) + L"\\" + step.path).c_str(), MOVEFILE_WRITE_THROUGH)) {
                return fail(L"Could not set aside " + step.path);
            }

            if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
                return fail(L"Could not put " + step.path + L" in place");
            }
        }
    }

    if (!writeTextFile(workFile(appDir, INSTALLED), L"1\n", CREATE_NEW)) {
        return fail(L"Could not record the finished install");
    }

    return InstallResult::Installed;
}

bool undoInstall(const std::wstring& appDir, std::wstring& error) {
    std::wstring                           text;
    std::vector<updaterules::JournalEntry> plan;

    if (!ownsWorkDir(appDir) || !readTextFile(workFile(appDir, JOURNAL_FILE), text)
        || !updaterules::parseJournal(text, plan) || plan.empty() || plan[0].kind != L'P') {
        error = L"The update journal is missing or damaged. The previous files, if any, are in " + backupDir(appDir);
        return false;
    }

    // First: a crash from here on means "finish undoing" at the next start.
    DeleteFileW(workFile(appDir, INSTALLED).c_str());

    const std::wstring packageDir = workDir(appDir) + L"\\" + plan[0].path;
    std::wstring       failed;
    int                trashCount = 0;

    // Each step checks what actually happened, so it is right whether the
    // install got that far or not.
    for (auto it = plan.rbegin(); it != plan.rend(); ++it) {
        const std::wstring target = appDir + L"\\" + it->path;

        if (it->kind == L'A') {
            if (fileExists(packageDir + L"\\" + it->path)) {
                continue;   // never moved in: whatever is at target isn't ours
            }

            if (!takeOut(appDir, target, trashCount)) {
                failed += L"\n" + it->path;
            }
        } else if (it->kind == L'R') {
            const std::wstring backup = backupDir(appDir) + L"\\" + it->path;

            if (!fileExists(backup)) {
                continue;   // never set aside: target is still the old file
            }

            if (testFails(L"LIMITNV_TEST_FAIL_RESTORE", it->path) || !takeOut(appDir, target, trashCount)
                || !MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
                failed += L"\n" + it->path;
            }
        } else if (it->kind == L'D') {
            RemoveDirectoryW(target.c_str());   // only if empty; fine if not
        }
    }

    if (!failed.empty()) {
        error = L"These files could not be put back:" + failed + L"\n\nThe previous versions are kept in "
              + backupDir(appDir);
        return false;
    }

    // Undone. Without the journal, whatever is left (the download, a trashed
    // running exe) is an ordinary leftover the next start deletes.
    DeleteFileW(workFile(appDir, JOURNAL_FILE).c_str());
    removeTree(workDir(appDir));
    return true;
}

void markConfirmed(const std::wstring& appDir) {
    if (ownsWorkDir(appDir)) {
        writeTextFile(workFile(appDir, CONFIRMED), L"1\n", CREATE_ALWAYS);
    }
}

void finishUpdate(const std::wstring& appDir) {
    if (ownsWorkDir(appDir)) {
        removeTree(workDir(appDir));
    }
}

Recovery recoverAtStartup(const std::wstring& appDir, std::wstring& error) {
    if (!ownsWorkDir(appDir)) {
        return Recovery::None;   // nothing there, or not ours: leave it alone
    }

    if (!fileExists(workFile(appDir, JOURNAL_FILE)) || fileExists(workFile(appDir, CONFIRMED))) {
        removeTree(workDir(appDir));   // an old download, or a finished update
        return Recovery::None;
    }

    if (fileExists(workFile(appDir, INSTALLED))) {
        return Recovery::PendingFinish;
    }

    return undoInstall(appDir, error) ? Recovery::Undone : Recovery::NeedsRepair;
}

} // namespace updater
