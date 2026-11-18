// limit-nvpstate-updater.exe: puts a downloaded update in place while
// limit-nvpstate is NOT running, so nothing is in use and every file is
// replaced in one rename. Then it starts the new version, waits for it to say
// it is up, and checks it is still running a moment later. Anything else: the
// previous files go back and the previous version is started again.
//
// No Qt, no NVAPI, static C runtime: it must run even when the program next
// to it can't. It never touches the GPU.
//
//   limit-nvpstate-updater.exe install <process handle> [--show]
//       started by limit-nvpstate with an inherited handle to itself; waits
//       for it to exit, then installs the update in limit-nvpstate-update\.
//   limit-nvpstate-updater.exe recover [<process handle>] [--show]
//   limit-nvpstate-updater.exe
//       finishes undoing an update that was interrupted (a crash, a power
//       cut). With a handle, first waits for that process to exit; without
//       one (run by hand), refuses while limit-nvpstate is running.
//
// Exit codes: 0 done (installed, rolled back, or nothing to do), 1 refused
// (bad arguments, another helper running, the old version didn't close),
// 2 needs repair (see result.txt / the message limit-nvpstate shows).

#include <testhooks.h>
#include <updaterules.h>
#include <updatetxn.h>

#include <Windows.h>
#include <shellapi.h>

#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

namespace {

using updaterules::Phase;

constexpr DWORD OLD_EXIT_MS  = 30000;   // the old version to close
constexpr DWORD READY_MS     = 30000;   // the new version to say it is up
constexpr DWORD GRACE_MS     = 3000;    // ...and still be running after that
constexpr DWORD TERMINATE_MS = 10000;   // a stopped new version to be gone
constexpr DWORD LOCK_MS      = 30000;

struct Options {
    enum class Mode { Install, Recover, Invalid } mode = Mode::Invalid;
    HANDLE waitFor = nullptr;   // the process to wait for before touching files
    bool   show    = false;
};

HANDLE inheritedProcess(const wchar_t* text) {
    wchar_t*                 end   = nullptr;
    const unsigned long long value = wcstoull(text, &end, 10);
    DWORD                    flags = 0;

    if (!end || *end != L'\0' || value == 0) {
        return nullptr;
    }

    const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value));
    return GetHandleInformation(handle, &flags) && GetProcessId(handle) != 0 ? handle : nullptr;
}

Options parseOptions() {
    Options options;
    int     count = 0;
    LPWSTR* args  = CommandLineToArgvW(GetCommandLineW(), &count);

    if (!args) {
        return options;
    }

    options.mode = count < 2 ? Options::Mode::Recover : Options::Mode::Invalid;

    for (int i = 1; i < count; i++) {
        if (i == 1 && wcscmp(args[i], L"install") == 0) {
            options.mode = Options::Mode::Install;
        } else if (i == 1 && wcscmp(args[i], L"recover") == 0) {
            options.mode = Options::Mode::Recover;
        } else if (wcscmp(args[i], L"--show") == 0) {
            options.show = true;
        } else if (i == 2 && options.mode != Options::Mode::Invalid) {
            options.waitFor = inheritedProcess(args[i]);

            if (!options.waitFor) {
                options.mode = Options::Mode::Invalid;
            }
        } else {
            options.mode = Options::Mode::Invalid;
        }
    }

    LocalFree(args);

    if (options.mode == Options::Mode::Install && !options.waitFor) {
        options.mode = Options::Mode::Invalid;   // install always comes from the app
    }

    return options;
}

// The program folder: where this exe is, or two up when it is the copy in
// limit-nvpstate-update\helper\.
std::wstring findAppDir() {
    wchar_t     path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);

    if (n == 0 || n >= MAX_PATH) {
        return {};
    }

    std::wstring dir = path;
    dir.erase(dir.find_last_of(L'\\'));

    const auto lastPart = [](const std::wstring& p) { return p.substr(p.find_last_of(L'\\') + 1); };
    const auto parentOf = [](const std::wstring& p) { return p.substr(0, p.find_last_of(L'\\')); };

    if (CompareStringOrdinal(lastPart(dir).c_str(), -1, L"helper", -1, TRUE) == CSTR_EQUAL
        && CompareStringOrdinal(lastPart(parentOf(dir)).c_str(), -1, L"limit-nvpstate-update", -1, TRUE)
               == CSTR_EQUAL) {
        dir = parentOf(parentOf(dir));
    }

    return updatetxn::fileExists(dir + L"\\limit-nvpstate.exe") ? dir : std::wstring();
}

// Starts limit-nvpstate. `ready` (optional) is the one handle it inherits.
HANDLE startApp(const std::wstring& appDir, bool show, HANDLE ready, DWORD& error, const wchar_t* extra = L"") {
    const std::wstring exe     = appDir + L"\\limit-nvpstate.exe";
    std::wstring       command = L"\"" + exe + L"\"" + extra;

    if (ready) {
        command += L" --after-update " + std::to_wstring(reinterpret_cast<uintptr_t>(ready));
    }

    if (show) {
        command += L" --show";
    }

    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);

    std::vector<char>            buffer(attributeSize ? attributeSize : 1);
    LPPROC_THREAD_ATTRIBUTE_LIST attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
    HANDLE                       inherited  = ready;

    // Only the event is inherited, nothing else of ours.
    const bool listed = ready && attributeSize > 0 && InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize)
                     && UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &inherited,
                                                  sizeof(inherited), nullptr, nullptr);

    if (ready && !listed) {
        error = GetLastError();
        return nullptr;
    }

    STARTUPINFOEXW      startup = {};
    PROCESS_INFORMATION process = {};
    startup.StartupInfo.cb      = sizeof(startup);
    startup.lpAttributeList     = listed ? attributes : nullptr;

    const BOOL started = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, ready ? TRUE : FALSE,
                                        listed ? EXTENDED_STARTUPINFO_PRESENT : 0, nullptr, appDir.c_str(),
                                        &startup.StartupInfo, &process);
    error = started ? 0 : GetLastError();

    if (listed) {
        DeleteProcThreadAttributeList(attributes);
    }

    if (!started) {
        return nullptr;
    }

    CloseHandle(process.hThread);
    return process.hProcess;
}

// Starts the program normally and lets it go (it shows what happened).
// "--recovered": it must not hand an unfinished update straight back to us.
void restartApp(const std::wstring& appDir, bool show) {
    DWORD  error   = 0;
    HANDLE process = startApp(appDir, show, nullptr, error, L" --recovered");

    if (process) {
        CloseHandle(process);
    }
}

std::wstring exitCodeText(HANDLE process) {
    DWORD code = 0;
    GetExitCodeProcess(process, &code);

    wchar_t text[16];
    swprintf(text, 16, L"0x%lX", code);
    return text;
}

// Undoes and records the outcome. The reason goes to result.txt, which the
// program shows when it starts. Returns the exit code.
int rollBack(const std::wstring& appDir, const std::wstring& reason) {
    std::wstring error;
    std::wstring phaseError;

    if (updatetxn::undo(appDir, error) && updatetxn::writePhase(appDir, Phase::RolledBack, phaseError)) {
        updatetxn::writeResult(appDir, reason + L"\n\nThe update was undone; you are on the previous version.");
        return 0;
    }

    updatetxn::writePhase(appDir, Phase::NeedsRepair, phaseError);
    updatetxn::writeResult(appDir, reason + L"\n\nThe update could not be fully undone.\n\n" + error);
    return 2;
}

int install(const std::wstring& appDir, HANDLE oldApp, bool show) {
    HANDLE lock = updatetxn::takeUpdateLock(LOCK_MS);

    if (!lock) {
        CloseHandle(oldApp);
        return 1;
    }

    // Nothing is touched until the old version is really gone: its exe and
    // DLLs are in use until then.
    const DWORD closed = WaitForSingleObject(oldApp, testValue(L"LIMITNV_TEST_OLD_EXIT_MS", OLD_EXIT_MS));
    CloseHandle(oldApp);

    if (closed != WAIT_OBJECT_0) {
        updatetxn::releaseUpdateLock(lock);
        return 1;   // it is still running; nothing was changed
    }

    std::wstring packageDir;
    std::wstring error;

    if (!updatetxn::findPackage(appDir, packageDir, error)) {
        std::wstring phaseError;
        updatetxn::writePhase(appDir, Phase::RolledBack, phaseError);
        updatetxn::writeResult(appDir, L"The update could not be installed, so nothing was changed.\n\n" + error);
        updatetxn::releaseUpdateLock(lock);
        restartApp(appDir, show);
        return 0;
    }

    const updatetxn::SwapResult swapped = updatetxn::swapIn(packageDir, appDir, error);

    if (swapped != updatetxn::SwapResult::Swapped) {
        std::wstring phaseError;

        if (swapped == updatetxn::SwapResult::RolledBack) {
            // Also when it failed before writing anything: the program must
            // still learn why (a phase is what it looks for).
            updatetxn::writePhase(appDir, Phase::RolledBack, phaseError);
            updatetxn::writeResult(appDir, L"The update could not be installed, so nothing was changed.\n\n" + error);
        } else {
            updatetxn::writeResult(appDir, L"The update could not be installed, and the previous version could not "
                                           L"be fully put back.\n\n" + error);
        }

        updatetxn::releaseUpdateLock(lock);
        restartApp(appDir, show);
        return swapped == updatetxn::SwapResult::RolledBack ? 0 : 2;
    }

    // ---- start the new version and check it is healthy
    int          result = 0;
    std::wstring reason;

    if (!updatetxn::writePhase(appDir, Phase::Verifying, error)) {
        reason = error;
    } else {
        SECURITY_ATTRIBUTES inheritable = { sizeof(inheritable), nullptr, TRUE };
        HANDLE              ready       = CreateEventW(&inheritable, TRUE, FALSE, nullptr);
        DWORD               launchError = 0;
        HANDLE              child       = ready ? startApp(appDir, show, ready, launchError) : nullptr;

        if (!child) {
            reason = updatetxn::errorText(L"The new version could not be started",
                                          ready ? launchError : GetLastError());
        } else {
            HANDLE      waits[] = { ready, child };
            const DWORD waited  = WaitForMultipleObjects(2, waits, FALSE,
                                                         testValue(L"LIMITNV_TEST_HANDSHAKE_MS", READY_MS));

            if (waited == WAIT_OBJECT_0) {
                // Said it is up. Still running a moment later?
                if (WaitForSingleObject(child, testValue(L"LIMITNV_TEST_GRACE_MS", GRACE_MS)) == WAIT_TIMEOUT) {
                    std::wstring phaseError;
                    updatetxn::writePhase(appDir, Phase::Committed, phaseError);
                    CloseHandle(child);
                    CloseHandle(ready);
                    updatetxn::releaseUpdateLock(lock);
                    return 0;
                }

                reason = L"The new version stopped right after starting (exit code " + exitCodeText(child) + L").";
            } else if (waited == WAIT_OBJECT_0 + 1) {
                reason = L"The new version stopped while starting (exit code " + exitCodeText(child) + L").";
            } else if (waited == WAIT_TIMEOUT) {
                reason = L"The new version did not finish starting in time.";
            } else {
                reason = updatetxn::errorText(L"Could not watch the new version start", GetLastError());
            }

            // It must be gone -- confirmed -- before its files are touched or
            // the previous version (another GPU manager) is started.
            if (WaitForSingleObject(child, 0) == WAIT_TIMEOUT) {
                TerminateProcess(child, 1);
            }

            DWORD gone = WaitForSingleObject(child, TERMINATE_MS);

            if (testFails(L"LIMITNV_TEST_FAIL_TERMINATE", L"1")) {
                gone = WAIT_TIMEOUT;
            }

            CloseHandle(child);

            if (gone != WAIT_OBJECT_0) {
                CloseHandle(ready);
                updatetxn::writeResult(appDir, reason + L"\n\nThe new version could not be stopped. Restart Windows; "
                                                        L"limit-nvpstate will then finish undoing the update.");
                updatetxn::releaseUpdateLock(lock);
                return 2;   // phase stays "verifying": the next start recovers
            }
        }

        if (ready) {
            CloseHandle(ready);
        }
    }

    result = rollBack(appDir, reason);
    updatetxn::releaseUpdateLock(lock);
    restartApp(appDir, show);
    return result;
}

bool appRunning() {
    HANDLE instance = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\limit-nvpstate");

    if (!instance) {
        return false;
    }

    const DWORD waited = WaitForSingleObject(instance, 0);

    if (waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED) {
        ReleaseMutex(instance);
    }

    CloseHandle(instance);
    return waited == WAIT_TIMEOUT;
}

int recover(const std::wstring& appDir, HANDLE waitFor, bool show) {
    if (waitFor) {
        const DWORD closed = WaitForSingleObject(waitFor, OLD_EXIT_MS);
        CloseHandle(waitFor);

        if (closed != WAIT_OBJECT_0) {
            return 1;
        }
    } else if (appRunning()) {
        MessageBoxW(nullptr, L"Close limit-nvpstate (right-click its tray icon, Exit), then run this again.",
                    L"limit-nvpstate updater", MB_ICONINFORMATION | MB_OK);
        return 1;
    }

    HANDLE lock = updatetxn::takeUpdateLock(LOCK_MS);

    if (!lock) {
        return 1;
    }

    int result = 0;

    if (updatetxn::ownsWorkDir(appDir)) {
        Phase        phase   = Phase::Swapping;
        bool         present = false;
        std::wstring error;

        if (!updatetxn::readPhase(appDir, phase, present, error)) {
            // Never guessed: an unreadable state is not "nothing to do".
            std::wstring phaseError;
            updatetxn::writePhase(appDir, Phase::NeedsRepair, phaseError);
            updatetxn::writeResult(appDir, error);
            result = 2;
        } else if (updatetxn::hasJournal(appDir)
                   && (!present || phase == Phase::Swapping || phase == Phase::Verifying
                       || phase == Phase::RollingBack || phase == Phase::NeedsRepair)) {
            // Interrupted, or an earlier undo that couldn't finish (a file was
            // locked): the undo checks every step, so trying again is safe.
            result = rollBack(appDir, phase == Phase::NeedsRepair && present
                                          ? L"An update could not be fully undone before; it was tried again."
                                          : L"An update was interrupted.");
        }
    }

    updatetxn::releaseUpdateLock(lock);
    restartApp(appDir, show);
    return result;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    const Options      options = parseOptions();
    const std::wstring appDir  = findAppDir();

    if (options.mode == Options::Mode::Invalid || appDir.empty()) {
        if (options.waitFor) {
            CloseHandle(options.waitFor);
        }

        return 1;
    }

    return options.mode == Options::Mode::Install ? install(appDir, options.waitFor, options.show)
                                                  : recover(appDir, options.waitFor, options.show);
}
