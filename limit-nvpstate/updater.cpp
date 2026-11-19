#include <updater.h>

#include <log.h>
#include <testhooks.h>
#include <updaterules.h>
#include <updatetxn.h>
#include <version.h>

#include <nlohmann/json.hpp>

#include <Windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

namespace updater {

namespace {

constexpr DWORD ACCEPT_MS = 45000;   // the helper to take over (it may wait 30 s for the update lock)

const wchar_t RELEASES_PAGE[] = L"https://github.com/NicholasBly/limit-nvpstate/releases";
const wchar_t LATEST_API[]    = L"https://api.github.com/repos/NicholasBly/limit-nvpstate/releases/latest";
const char    DOWNLOADS[]     = "https://github.com/NicholasBly/limit-nvpstate/releases/download/";   // same repo

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
// Where the release's files must be: "<downloads>/<tag>/<name>" on GitHub;
// in test builds, "<the test server>/dl/<name>" (the rig's layout).
std::string downloadPrefix(const std::string& tag) {
    const std::wstring api    = latestApiUrl();
    const size_t       scheme = api.find(L"://");
    const size_t       path   = scheme == std::wstring::npos ? std::wstring::npos : api.find(L'/', scheme + 3);

    if (api == LATEST_API || path == std::wstring::npos) {
        return std::string(DOWNLOADS) + tag + "/";
    }

    return std::string(api.begin(), api.begin() + static_cast<std::ptrdiff_t>(path)) + "/dl/";   // ASCII URL
}
#else
bool         allowHttp() { return false; }
std::wstring latestApiUrl() { return LATEST_API; }
std::string  downloadPrefix(const std::string& tag) { return std::string(DOWNLOADS) + tag + "/"; }
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

using updatetxn::errorText;
using updatetxn::fileExists;

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

bool runTar(const std::wstring& zipPath, const std::wstring& outDir, std::wstring& error) {
    wchar_t    system[MAX_PATH];
    const UINT n = GetSystemDirectoryW(system, MAX_PATH);

    if (n == 0 || n >= MAX_PATH) {
        error = L"Could not find the Windows folder";
        return false;
    }

    std::wstring tar = std::wstring(system) + L"\\tar.exe";

    if (std::wstring other; testText(L"LIMITNV_TEST_TAR", other)) {
        tar = other;   // test only: the rig's stand-in, for tests that need it to misbehave
    }

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

// The zip's own file list (its central directory), read before tar runs.
bool readZipDirectory(const std::wstring& path, std::vector<updaterules::ZipEntry>& entries, std::wstring& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        error = errorText(L"Could not open the download", GetLastError());
        return false;
    }

    const auto readAt = [&](unsigned long long offset, size_t bytes, std::string& out) {
        LARGE_INTEGER at;
        at.QuadPart = static_cast<LONGLONG>(offset);
        out.assign(bytes, '\0');

        DWORD read = 0;
        return SetFilePointerEx(file, at, nullptr, FILE_BEGIN)
            && ReadFile(file, out.data(), static_cast<DWORD>(bytes), &read, nullptr) && read == bytes;
    };

    LARGE_INTEGER      size = {};
    std::string        tail;
    std::string        directory;
    unsigned long long offset = 0;
    unsigned long long length = 0;
    unsigned           count  = 0;

    const bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0
                 && readAt(static_cast<unsigned long long>(size.QuadPart)
                               - std::min<unsigned long long>(static_cast<unsigned long long>(size.QuadPart), 65557),
                           static_cast<size_t>(std::min<LONGLONG>(size.QuadPart, 65557)), tail)
                 && updaterules::findZipDirectory(tail, static_cast<unsigned long long>(size.QuadPart), offset, length,
                                                  count)
                 && length <= (16u << 20) && readAt(offset, static_cast<size_t>(length), directory)
                 && updaterules::parseZipDirectory(directory, count, entries);

    CloseHandle(file);

    if (!ok) {
        error = L"The download is not a zip this updater can read";
    }

    return ok;
}

// Starts the helper with an inherited handle to this process (it waits for
// us to exit before touching anything); that handle is the only one it gets.
// Starts the helper and waits until it has taken over (or has ended, or a
// deadline passed). Only then may this process close: a helper that dies at
// its start would otherwise leave nothing running and nobody to say so. The
// helper inherits two handles and nothing else: this process (to wait for)
// and the "accepted" event.
HandOff startHelper(const std::wstring& helperExe, const std::wstring& appDir, const wchar_t* mode, bool show,
                    std::wstring& error) {
    HANDLE self = nullptr;

    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &self,
                         SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, 0)) {
        error = errorText(L"Could not prepare the updater", GetLastError());
        return HandOff::Refused;
    }

    SECURITY_ATTRIBUTES inheritable = { sizeof(inheritable), nullptr, TRUE };
    HANDLE              accepted    = CreateEventW(&inheritable, TRUE, FALSE, nullptr);

    if (!accepted) {
        error = errorText(L"Could not prepare the updater", GetLastError());
        CloseHandle(self);
        return HandOff::Refused;
    }

    std::wstring command = L"\"" + helperExe + L"\" " + mode + L" " + std::to_wstring(reinterpret_cast<uintptr_t>(self))
                         + L" " + std::to_wstring(reinterpret_cast<uintptr_t>(accepted));

    if (show) {
        command += L" --show";
    }

    HANDLE inherited[] = { self, accepted };
    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);

    std::vector<char>            buffer(attributeSize ? attributeSize : 1);
    LPPROC_THREAD_ATTRIBUTE_LIST attributes  = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
    const bool                   initialized = attributeSize > 0
                                && InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize);
    const bool listed = initialized
                     && UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                                  sizeof(inherited), nullptr, nullptr);

    STARTUPINFOEXW      startup = {};
    PROCESS_INFORMATION process = {};
    startup.StartupInfo.cb      = sizeof(startup);
    startup.lpAttributeList     = attributes;

    const BOOL  started     = listed
                         && CreateProcessW(helperExe.c_str(), command.data(), nullptr, nullptr, TRUE,
                                           EXTENDED_STARTUPINFO_PRESENT, nullptr, appDir.c_str(), &startup.StartupInfo,
                                           &process);
    const DWORD launchError = GetLastError();

    if (initialized) {
        DeleteProcThreadAttributeList(attributes);
    }

    CloseHandle(self);   // the helper has its own copy now (or never will)

    if (!started) {
        CloseHandle(accepted);
        error = errorText(L"Could not start the updater", launchError);
        return HandOff::Refused;
    }

    CloseHandle(process.hThread);

    testPause(L"LIMITNV_TEST_PAUSE_BEFORE_HANDOFF_WAIT");   // test only: the helper may have answered and ended

    HANDLE      waits[] = { accepted, process.hProcess };
    const DWORD waited  = WaitForMultipleObjects(2, waits, FALSE, testValue(L"LIMITNV_TEST_ACCEPT_MS", ACCEPT_MS));
    HandOff     result  = HandOff::Refused;

    if (waited == WAIT_OBJECT_0 && WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
        result = HandOff::Accepted;   // it answered, and it is still there to do the job
    } else if (waited == WAIT_OBJECT_0 || waited == WAIT_OBJECT_0 + 1) {
        // Ended -- before answering, or right after (when both are seen at
        // once, "accepted" alone would win). It changes files only after this
        // process has closed, so nothing was changed.
        DWORD              code   = 0;
        const bool         known  = GetExitCodeProcess(process.hProcess, &code) != FALSE;
        const std::wstring reason = updatetxn::readResult(appDir);

        // The helper's exit codes from before it takes over (updater-helper/main.cpp):
        // 3 = EXIT_BUSY, 4 = EXIT_NO_PACKAGE (the reason is in result.txt), 5 = EXIT_UNSUPPORTED.
        if (waited == WAIT_OBJECT_0) {
            error = L"The updater stopped right after it took over";
        } else if (known && code == 3) {
            error = L"Another update is being installed right now";
        } else if (known && code == 4 && !reason.empty()) {
            error = reason;
        } else if (known && code == 5) {
            error = L"This update can't be installed from this version. Download it from the releases page and unzip "
                    L"it over this folder";
        } else {
            error = L"The updater stopped before it took over (exit code " + std::to_wstring(code) + L")";
        }
    } else {
        // No answer in time (or the wait failed). It has touched nothing yet
        // -- that comes only after "accepted" and this process closing -- so
        // it is stopped; but only a confirmed stop makes it over.
        TerminateProcess(process.hProcess, 1);
        DWORD gone = WaitForSingleObject(process.hProcess, 5000);

        if (testFails(L"LIMITNV_TEST_FAIL_HELPER_TERMINATE", L"1")) {
            gone = WAIT_TIMEOUT;   // test only: it could not be confirmed stopped
        }

        if (gone == WAIT_OBJECT_0) {
            error = L"The updater did not respond";
        } else {
            result = HandOff::Unresolved;
            error  = L"The updater did not respond and could not be stopped. Restart Windows before you try to update "
                     L"again";
        }
    }

    CloseHandle(accepted);
    CloseHandle(process.hProcess);
    return result;
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

    // From this project's own release downloads, exactly: a release that
    // lists its files anywhere else is not downloaded.
    const std::string prefix = downloadPrefix(out.version);
    const size_t      zip    = static_cast<size_t>(choice.zip);

    if (!updaterules::isExpectedAssetUrl(urls[zip], prefix, names[zip])
        || (choice.checksum >= 0
            && !updaterules::isExpectedAssetUrl(urls[static_cast<size_t>(choice.checksum)], prefix,
                                                names[static_cast<size_t>(choice.checksum)]))) {
        error = L"The release's download address is not the expected one, so it was not downloaded";
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

    if (cancelled() || !updatetxn::openWorkDir(appDir, error)) {
        return false;
    }

    const std::wstring download   = updatetxn::downloadDir(appDir);
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

    // ---- the zip's own list decides before anything is unpacked: one top
    // folder, plain files with safe names, sane sizes, every required file
    std::vector<updaterules::ZipEntry> entries;
    std::wstring                       top;

    if (!readZipDirectory(zipPath, entries, error)) {
        return false;
    }

    error = updaterules::zipProblem(entries, top);

    if (!error.empty() || cancelled()) {
        return false;
    }

    // ---- unpack, and the result must be exactly that list
    const std::wstring unpacked = download + L"\\unpacked";

    if (!CreateDirectoryW(unpacked.c_str(), nullptr)) {
        error = errorText(L"Could not create the unpack folder", GetLastError());
        return false;
    }

    if (!runTar(zipPath, unpacked, error) || cancelled()) {
        return false;
    }

    const std::wstring        root = unpacked + L"\\" + top;
    std::vector<std::wstring> files;
    unsigned long long        bytes = 0;

    if (!updatetxn::directoryExists(root) || !updatetxn::listPackage(root, L"", files, bytes, 0, error)) {
        if (error.empty()) error = L"The update package did not unpack as expected";
        return false;
    }

    std::vector<std::wstring> listed;

    for (const updaterules::ZipEntry& entry : entries) {
        if (!entry.directory) {
            std::wstring name(entry.name.begin(), entry.name.end());   // ASCII, checked by zipProblem
            std::replace(name.begin(), name.end(), L'/', L'\\');
            listed.push_back(name.substr(top.size() + 1));
        }
    }

    const auto byName = [](const std::wstring& a, const std::wstring& b) {
        return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    };

    std::sort(files.begin(), files.end(), byName);
    std::sort(listed.begin(), listed.end(), byName);

    const bool same = files.size() == listed.size()
                   && std::equal(files.begin(), files.end(), listed.begin(), [](const std::wstring& a, const std::wstring& b) {
                          return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
                      });

    // Names alone aren't enough: the unzipper is a different program from
    // the check above, so every file must also have the size the zip's list
    // gives, and nothing may have been unpacked beside the package folder.
    bool sizesMatch = same;

    for (const updaterules::ZipEntry& entry : entries) {
        if (!sizesMatch || entry.directory) {
            continue;
        }

        std::wstring name(entry.name.begin(), entry.name.end());
        std::replace(name.begin(), name.end(), L'/', L'\\');

        WIN32_FILE_ATTRIBUTE_DATA data = {};
        sizesMatch = GetFileAttributesExW((unpacked + L"\\" + name).c_str(), GetFileExInfoStandard, &data)
                  && ((static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow) == entry.size;
    }

    bool onlyPackage = true;
    WIN32_FIND_DATAW found;
    HANDLE           find = FindFirstFileW((unpacked + L"\\*").c_str(), &found);

    if (find == INVALID_HANDLE_VALUE) {
        onlyPackage = false;
    } else {
        do {
            const std::wstring child = found.cFileName;
            onlyPackage &= child == L"." || child == L".."
                        || CompareStringOrdinal(child.c_str(), -1, top.c_str(), -1, TRUE) == CSTR_EQUAL;
        } while (FindNextFileW(find, &found));

        onlyPackage &= GetLastError() == ERROR_NO_MORE_FILES;
        FindClose(find);
    }

    if (!same || !sizesMatch || !onlyPackage) {
        error = L"The unpacked files don't match the update package's list, so it was not installed";
        return false;
    }

    // The limits on the real files, before any of them (the helper) runs.
    error = updaterules::packageProblem(files, bytes);

    if (!error.empty()) {
        return false;
    }

    // ---- the helper that will do the swap, kept apart from the package
    const std::wstring helper    = updatetxn::helperPath(appDir);
    const std::wstring helperDir = helper.substr(0, helper.find_last_of(L'\\'));

    if (!CreateDirectoryW(helperDir.c_str(), nullptr)
        || !CopyFileW((root + L"\\limit-nvpstate-updater.exe").c_str(), helper.c_str(), TRUE)
        || !updatetxn::flushToDisk(helper)) {
        error = errorText(L"Could not set up the updater", GetLastError());
        return false;
    }

    packageDir = root;
    return true;
}

void discardDownload(const std::wstring& appDir) {
    updatetxn::discardDownload(appDir);
}

HandOff launchHelper(const std::wstring& appDir, bool show, std::wstring& error) {
    return startHelper(updatetxn::helperPath(appDir), appDir, L"install", show, error);
}

StartupState checkAtStartup(const std::wstring& appDir, bool justRecovered, std::wstring& message) {
    using updaterules::Phase;

    if (!updatetxn::ownsWorkDir(appDir)) {
        return StartupState::Normal;
    }

    // No journal: no program file was touched -- an old download, or an
    // update that failed before it started (finishUpdateLater says why).
    if (!updatetxn::hasJournal(appDir)) {
        return StartupState::Normal;
    }

    Phase        phase   = Phase::Swapping;
    bool         present = false;
    std::wstring error;

    if (!updatetxn::readPhase(appDir, phase, present, error)) {
        message = error + L"\n\nThe files from before the update are in " + updatetxn::backupDir(appDir);
        return StartupState::NeedsRepair;
    }

    if (present && (phase == Phase::Committed || phase == Phase::RolledBack)) {
        return StartupState::Finished;
    }

    // Swapping, verifying, rolling back or needing repair, and no helper
    // running (the caller checked the lock): the helper died, or could not
    // put everything back. It is started to (try again to) undo it, then
    // starts the program again -- unless it just did.
    if (justRecovered) {
        const std::wstring result = updatetxn::readResult(appDir);
        message = (result.empty() ? std::wstring(L"An update could not be fully undone.") : result)
                + L"\n\nThe files from before the update are in " + updatetxn::backupDir(appDir) + L".";
        return StartupState::NeedsRepair;
    }

    const std::wstring copy      = updatetxn::helperPath(appDir);
    const std::wstring installed = appDir + L"\\limit-nvpstate-updater.exe";

    if (startHelper(updatetxn::fileExists(copy) ? copy : installed, appDir, L"recover", false, error) == HandOff::Accepted) {
        return StartupState::Recovering;
    }

    message = L"An update did not finish, and the updater could not be started to undo it (" + error
            + L").\n\nThe files from before the update are in " + updatetxn::backupDir(appDir);
    return StartupState::NeedsRepair;
}

} // namespace updater
