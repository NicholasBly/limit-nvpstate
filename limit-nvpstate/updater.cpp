#include <updater.h>

#include <log.h>
#include <updaterules.h>
#include <version.h>

#include <nlohmann/json.hpp>

#include <Windows.h>
#include <bcrypt.h>
#include <winhttp.h>

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

    WinHttpSetTimeouts(session, 15000, 15000, 15000, 30000);

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
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)
        || status != 200) {
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

bool sha256File(const std::wstring& path, std::string& hexOut) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_FLAG_SEQUENTIAL_SCAN, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    BCRYPT_ALG_HANDLE  algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash      = nullptr;
    bool               ok        = false;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0
        && BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<unsigned char> buffer(64 * 1024);
        DWORD                      read = 0;

        ok = true;

        while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) && read > 0) {
            if (BCryptHashData(hash, buffer.data(), read, 0) != 0) {
                ok = false;
                break;
            }
        }

        unsigned char digest[32];

        if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0) {
            static const char hex[] = "0123456789abcdef";
            hexOut.clear();

            for (unsigned char b : digest) {
                hexOut += hex[b >> 4];
                hexOut += hex[b & 15];
            }
        } else {
            ok = false;
        }
    }

    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    return ok;
}

bool fileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool directoryExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// Every file under `root`, as paths relative to it.
void listFiles(const std::wstring& root, const std::wstring& relative, std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW data;
    const std::wstring pattern = root + L"\\" + (relative.empty() ? L"" : relative + L"\\") + L"*";
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);

    if (find == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        const std::wstring name = data.cFileName;

        if (name == L"." || name == L"..") {
            continue;
        }

        const std::wstring child = relative.empty() ? name : relative + L"\\" + name;

        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            continue;   // never follow links out of the package
        }

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            listFiles(root, child, out);
        } else {
            out.push_back(child);
        }
    } while (FindNextFileW(find, &data));

    FindClose(find);
}

bool makeParentDirectories(const std::wstring& appDir, const std::wstring& relative) {
    size_t slash = relative.find(L'\\');

    while (slash != std::wstring::npos) {
        const std::wstring dir = appDir + L"\\" + relative.substr(0, slash);

        if (!directoryExists(dir) && !CreateDirectoryW(dir.c_str(), nullptr)) {
            return false;
        }

        slash = relative.find(L'\\', slash + 1);
    }

    return true;
}

bool runTar(const std::wstring& zipPath, const std::wstring& outDir, std::wstring& error) {
    wchar_t system[MAX_PATH];
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
        TerminateProcess(process.hProcess, 1);
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    if (code != 0) {
        error = L"Unpacking the update failed (tar.exe exit code " + std::to_wstring(code) + L")";
        return false;
    }

    return true;
}

} // namespace

const wchar_t* releasesPageUrl() {
    return RELEASES_PAGE;
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

    const updaterules::AssetChoice choice = updaterules::chooseAssets(names);

    out         = Release{};
    out.version = tag->get<std::string>();
    out.pageUrl = (page != reply.end() && page->is_string()) ? widen(page->get<std::string>()) : RELEASES_PAGE;

    if (choice.zip < 0) {
        error = L"The latest release has no limit-nvpstate zip";
        return false;
    }

    out.zipName = widen(names[static_cast<size_t>(choice.zip)]);
    out.zipUrl  = widen(urls[static_cast<size_t>(choice.zip)]);

    if (choice.checksum >= 0) {
        out.checksumUrl = widen(urls[static_cast<size_t>(choice.checksum)]);
    }

    // The zip name becomes a file name in our temp folder.
    if (!updaterules::isSafeRelativePath(out.zipName) || out.zipName.find_first_of(L"\\/") != std::wstring::npos) {
        error = L"The release zip has an unexpected name";
        return false;
    }

    return true;
}

bool prepare(const Release&  release,
             std::wstring&   packageDir,
             std::wstring&   tempDir,
             std::wstring&   error,
             const Progress& progress) {
    if (release.checksumUrl.empty()) {
        error = L"This release has no checksum file, so it can't be verified";
        return false;
    }

    // A fresh folder every time: nothing left over from an earlier attempt
    // can end up in the package.
    wchar_t temp[MAX_PATH];
    const DWORD n = GetTempPathW(MAX_PATH, temp);

    if (n == 0 || n >= MAX_PATH) {
        error = L"Could not find the temp folder";
        return false;
    }

    for (unsigned attempt = 0;; attempt++) {
        tempDir = std::wstring(temp) + L"limit-nvpstate-update-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);

        if (CreateDirectoryW(tempDir.c_str(), nullptr)) {
            break;
        }

        if (GetLastError() != ERROR_ALREADY_EXISTS || attempt > 20) {
            error = errorText(L"Could not create a temp folder", GetLastError());
            tempDir.clear();
            return false;
        }
    }

    // ---- checksum first: small, and no point downloading 20 MB without it
    std::string checksumText;

    if (!httpGet(release.checksumUrl, [&](const char* data, DWORD got) { checksumText.append(data, got); return true; },
                 MAX_CHECKSUM_BYTES, error, nullptr)) {
        error = L"Could not download the checksum: " + error;
        return false;
    }

    std::string expected;

    if (!updaterules::parseChecksumFile(checksumText, narrow(release.zipName), expected)) {
        error = L"The release's checksum file is not readable";
        return false;
    }

    // ---- the zip
    const std::wstring zipPath = tempDir + L"\\" + release.zipName;

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
        error = L"Could not download the update: " + error;
        return false;
    }

    std::string actual;

    if (!sha256File(zipPath, actual)) {
        error = errorText(L"Could not check the download", GetLastError());
        return false;
    }

    if (actual != expected) {
        error = L"The download does not match the release's checksum, so it was not installed";
        return false;
    }

    // ---- unpack and find the folder holding the program
    const std::wstring unpacked = tempDir + L"\\unpacked";

    if (!CreateDirectoryW(unpacked.c_str(), nullptr) || !runTar(zipPath, unpacked, error)) {
        if (error.empty()) error = errorText(L"Could not create a temp folder", GetLastError());
        return false;
    }

    if (fileExists(unpacked + L"\\limit-nvpstate.exe")) {
        packageDir = unpacked;
        return true;
    }

    WIN32_FIND_DATAW data;
    HANDLE           find = FindFirstFileW((unpacked + L"\\*").c_str(), &data);

    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = data.cFileName;

            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && name != L"." && name != L".."
                && fileExists(unpacked + L"\\" + name + L"\\limit-nvpstate.exe")) {
                packageDir = unpacked + L"\\" + name;
                break;
            }
        } while (FindNextFileW(find, &data));

        FindClose(find);
    }

    if (packageDir.empty()) {
        error = L"The update package does not contain limit-nvpstate.exe";
        return false;
    }

    return true;
}

bool undoInstall(const InstallJournal& journal) {
    bool ok = true;

    for (auto it = journal.steps.rbegin(); it != journal.steps.rend(); ++it) {
        if (it->placed && !DeleteFileW(it->target.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) {
            ok = false;
        }

        if (it->keptOld
            && !MoveFileExW((it->target + L".old").c_str(), it->target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            ok = false;
        }
    }

    return ok;
}

bool install(const std::wstring& packageDir, const std::wstring& appDir, InstallJournal& journal,
             std::wstring& error) {
    std::vector<std::wstring> files;
    listFiles(packageDir, L"", files);

    journal.steps.clear();

    const auto rollBack = [&]() {
        undoInstall(journal);
        journal.steps.clear();
    };

    for (const std::wstring& relative : files) {
        if (!updaterules::isSafeRelativePath(relative)) {
            error = L"The update package contains an unsafe path: " + relative;
            rollBack();
            return false;
        }

        const std::wstring source = packageDir + L"\\" + relative;
        const std::wstring target = appDir + L"\\" + relative;

        // Your settings stay yours.
        if (CompareStringOrdinal(relative.c_str(), -1, L"config.json", -1, TRUE) == CSTR_EQUAL && fileExists(target)) {
            continue;
        }

        if (!makeParentDirectories(appDir, relative)) {
            error = errorText((L"Could not create the folder for " + relative).c_str(), GetLastError());
            rollBack();
            return false;
        }

        InstallJournal::Step step;
        step.target = target;

        if (fileExists(target)) {
            const std::wstring old = target + L".old";
            DeleteFileW(old.c_str());   // from an earlier update; fine if it fails

            if (!MoveFileExW(target.c_str(), old.c_str(), 0)) {
                error = errorText((L"Could not set aside " + relative).c_str(), GetLastError());
                rollBack();
                return false;
            }

            step.keptOld = true;
        }

        if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED)) {
            error = errorText((L"Could not put " + relative + L" in place").c_str(), GetLastError());
            journal.steps.push_back(step);
            rollBack();
            return false;
        }

        step.placed = true;
        journal.steps.push_back(step);
    }

    return true;
}

void removeLeftovers(const std::wstring& appDir) {
    std::vector<std::wstring> files;
    listFiles(appDir, L"", files);

    for (const std::wstring& relative : files) {
        const size_t n = relative.size();

        if (n > 4 && CompareStringOrdinal(relative.c_str() + n - 4, 4, L".old", 4, TRUE) == CSTR_EQUAL
            && fileExists(appDir + L"\\" + relative.substr(0, n - 4))) {
            DeleteFileW((appDir + L"\\" + relative).c_str());
        }
    }
}

void removeTree(const std::wstring& dir) {
    if (dir.empty()) {
        return;
    }

    // Something can hold a file or folder open for a moment after we are done
    // with it -- tar.exe's working folder as it exits, a virus scanner looking
    // at the fresh files -- and deleting then fails with a sharing violation.
    // So retry with short waits (about 3 s at most) instead of leaving the
    // download behind in %TEMP%.
    for (int pass = 0; pass < 10; pass++) {
        if (pass > 0) {
            Sleep(pass < 3 ? 100u << (pass - 1) : 400u);
        }

        std::vector<std::wstring> files;
        listFiles(dir, L"", files);

        for (const std::wstring& relative : files) {
            DeleteFileW((dir + L"\\" + relative).c_str());
        }

        // Then the folders, deepest first.
        WIN32_FIND_DATAW          data;
        std::vector<std::wstring> stack{ dir };
        std::vector<std::wstring> all;

        while (!stack.empty()) {
            const std::wstring current = stack.back();
            stack.pop_back();
            all.push_back(current);

            HANDLE find = FindFirstFileW((current + L"\\*").c_str(), &data);

            if (find == INVALID_HANDLE_VALUE) {
                continue;
            }

            do {
                const std::wstring name = data.cFileName;

                if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && name != L"." && name != L".."
                    && !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    stack.push_back(current + L"\\" + name);
                }
            } while (FindNextFileW(find, &data));

            FindClose(find);
        }

        for (auto it = all.rbegin(); it != all.rend(); ++it) {
            RemoveDirectoryW(it->c_str());
        }

        if (!directoryExists(dir)) {
            return;
        }
    }
}

} // namespace updater
