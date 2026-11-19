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
constexpr DWORD APP_GONE_MS  = 10000;   // no other copy may be running
constexpr int   COMMIT_TRIES    = 5;     // writing "committed"...
constexpr DWORD COMMIT_RETRY_MS = 200;   // ...this far apart

// Exit codes. The program reads the ones from before the helper took over.
constexpr int EXIT_OK          = 0;
constexpr int EXIT_REFUSED     = 1;
constexpr int EXIT_NEEDS_REPAIR = 2;
constexpr int EXIT_BUSY        = 3;   // another update holds the lock
constexpr int EXIT_NO_PACKAGE  = 4;   // the reason is in result.txt

struct Options {
    enum class Mode { Install, Recover, Invalid } mode = Mode::Invalid;
    HANDLE waitFor  = nullptr;   // the process to wait for before touching files
    HANDLE accepted = nullptr;   // set once this helper has taken over
    bool   show     = false;
};

HANDLE inheritedEvent(const wchar_t* text) {
    wchar_t*                 end   = nullptr;
    const unsigned long long value = wcstoull(text, &end, 10);
    DWORD                    flags = 0;

    if (!end || *end != L'\0' || value == 0) {
        return nullptr;
    }

    const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value));
    return GetHandleInformation(handle, &flags) && WaitForSingleObject(handle, 0) != WAIT_FAILED ? handle : nullptr;
}

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
        } else if (i == 3 && options.waitFor) {
            options.accepted = inheritedEvent(args[i]);

            if (!options.accepted) {
                options.mode = Options::Mode::Invalid;
            }
        } else {
            options.mode = Options::Mode::Invalid;
        }
    }

    LocalFree(args);

    if (options.mode == Options::Mode::Install && (!options.waitFor || !options.accepted)) {
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

    // Not "where limit-nvpstate.exe is": recovery is exactly for when it may
    // be missing (an interrupted undo, a virus scanner). recover() checks the
    // updater's own folder and journal instead.
    return dir;
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
        return;
    }

    // Nothing else is running to tell the user, and the GPU is unmanaged.
    const std::wstring text = updatetxn::errorText(L"limit-nvpstate could not be started again after the update", error) +
                              L"\n\nStart limit-nvpstate.exe from its folder.";
    MessageBoxW(nullptr, text.c_str(), L"limit-nvpstate updater", MB_ICONWARNING | MB_OK);
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

    // The reason is written before the outcome it explains: the program
    // shows the reason when it finds the outcome.
    if (updatetxn::undo(appDir, error)) {
        updatetxn::writeResult(appDir, reason + L"\n\nThe update was undone; you are on the previous version.");

        if (updatetxn::writePhase(appDir, Phase::RolledBack, phaseError)) {
            return EXIT_OK;
        }

        error = phaseError;
    }

    updatetxn::writeResult(appDir, reason + L"\n\nThe update could not be fully undone.\n\n" + error);
    updatetxn::writePhase(appDir, Phase::NeedsRepair, phaseError);
    return EXIT_NEEDS_REPAIR;
}

// A copy owns the single-instance mutex. Anything that can't be checked
// counts as running.
bool appRunning() {
    HANDLE instance = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\limit-nvpstate");

    if (!instance) {
        return GetLastError() != ERROR_FILE_NOT_FOUND;
    }

    const DWORD waited = WaitForSingleObject(instance, 0);

    if (waited == WAIT_OBJECT_0 || waited == WAIT_ABANDONED) {
        ReleaseMutex(instance);
    }

    CloseHandle(instance);
    return waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED;
}

// Called holding the update lock. A copy that starts now takes the mutex
// first and then sees the lock, so it gives way in a moment; this waits for
// that, and for nothing longer.
bool waitForNoApp(DWORD ms) {
    for (DWORD waited = 0;; waited += 100) {
        if (!appRunning()) {
            return true;
        }

        if (waited >= ms) {
            return false;
        }

        Sleep(100);
    }
}

// From here on this helper owns the update; the program may close.
void acknowledge(HANDLE& accepted) {
    if (accepted) {
        SetEvent(accepted);
        CloseHandle(accepted);
        accepted = nullptr;
    }
}

void closeIfSet(HANDLE& handle) {
    if (handle) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

int install(const std::wstring& appDir, HANDLE oldApp, HANDLE accepted, bool show) {
    if (testFails(L"LIMITNV_TEST_HELPER_EXIT_EARLY", L"install")) {
        CloseHandle(oldApp);
        closeIfSet(accepted);
        return EXIT_REFUSED;   // test only: the helper dies right after it starts
    }

    // ---- before taking over: on any problem the program is still running,
    // and keeps running (it sees this process end without "accepted").
    HANDLE lock = updatetxn::takeUpdateLock(LOCK_MS);

    if (!lock) {
        CloseHandle(oldApp);
        closeIfSet(accepted);
        return EXIT_BUSY;
    }

    std::wstring packageDir;
    std::wstring error;

    if (!updatetxn::findPackage(appDir, packageDir, error)) {
        updatetxn::writeResult(appDir, L"The update could not be installed, so nothing was changed.\n\n" + error);
        updatetxn::releaseUpdateLock(lock);
        CloseHandle(oldApp);
        closeIfSet(accepted);
        return EXIT_NO_PACKAGE;
    }

    acknowledge(accepted);

    // Nothing is touched until the old version is really gone: its exe and
    // DLLs are in use until then.
    const DWORD closed = WaitForSingleObject(oldApp, testValue(L"LIMITNV_TEST_OLD_EXIT_MS", OLD_EXIT_MS));

    if (closed != WAIT_OBJECT_0) {
        // Nothing was changed. It is still closing (it said it would), so
        // once it is gone it is started again -- otherwise nothing would be
        // running. The lock is let go first: it would block a start by hand.
        std::wstring phaseError;
        updatetxn::writeResult(appDir, L"The previous version took too long to close, so the update was not "
                                       L"installed. Nothing was changed.");
        updatetxn::writePhase(appDir, Phase::RolledBack, phaseError);
        updatetxn::releaseUpdateLock(lock);
        WaitForSingleObject(oldApp, INFINITE);
        CloseHandle(oldApp);
        restartApp(appDir, show);
        return 1;
    }

    CloseHandle(oldApp);

    // No other copy may be running while the files change: it would hold
    // them open, or run on a mix of old and new files.
    if (!waitForNoApp(APP_GONE_MS)) {
        std::wstring phaseError;
        updatetxn::writeResult(appDir, L"Another copy of limit-nvpstate was running, so the update was not "
                                       L"installed. Nothing was changed.");
        updatetxn::writePhase(appDir, Phase::RolledBack, phaseError);
        updatetxn::releaseUpdateLock(lock);
        return EXIT_REFUSED;   // that copy is the GPU manager; nothing to start
    }

    const updatetxn::SwapResult swapped = updatetxn::swapIn(packageDir, appDir, error);

    if (swapped != updatetxn::SwapResult::Swapped) {
        std::wstring phaseError;

        if (swapped == updatetxn::SwapResult::RolledBack) {
            // Also when it failed before writing anything: the program must
            // still learn why (a phase is what it looks for).
            updatetxn::writeResult(appDir, L"The update could not be installed, so nothing was changed.\n\n" + error);
            updatetxn::writePhase(appDir, Phase::RolledBack, phaseError);
        } else {
            updatetxn::writeResult(appDir, L"The update could not be installed, and the previous version could not "
                                           L"be fully put back.\n\n" + error);
        }

        updatetxn::releaseUpdateLock(lock);
        restartApp(appDir, show);
        return swapped == updatetxn::SwapResult::RolledBack ? EXIT_OK : EXIT_NEEDS_REPAIR;
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
                    bool         committed = false;

                    // A brief lock on phase.txt (a virus scanner) must not
                    // undo a working update.
                    bool         died      = false;

                    for (int attempt = 0; attempt < COMMIT_TRIES && !committed && !died; attempt++) {
                        if (attempt > 0) {
                            Sleep(COMMIT_RETRY_MS);
                        }

                        // Test only: the new version dies between two tries.
                        if (attempt + 1 == static_cast<int>(testValue(L"LIMITNV_TEST_KILL_CHILD_AT_COMMIT_TRY", 0))) {
                            TerminateProcess(child, 7);
                            WaitForSingleObject(child, 5000);
                        }

                        // Recorded as done only while it is running: one that
                        // died meanwhile is undone, not committed with nothing
                        // running.
                        if (WaitForSingleObject(child, 0) != WAIT_TIMEOUT) {
                            died = true;
                            break;
                        }

                        if (testFails(L"LIMITNV_TEST_FAIL_COMMIT", L"1")
                            || attempt < static_cast<int>(testValue(L"LIMITNV_TEST_FAIL_COMMIT_TIMES", 0))) {
                            phaseError = L"injected failure";
                        } else {
                            committed = updatetxn::writePhase(appDir, Phase::Committed, phaseError);
                        }
                    }

                    if (committed) {
                        CloseHandle(child);
                        CloseHandle(ready);
                        updatetxn::releaseUpdateLock(lock);
                        return EXIT_OK;
                    }

                    if (died) {
                        reason = L"The new version stopped right after starting (exit code " + exitCodeText(child) + L").";
                    } else {
                        // Left as "verifying", the next start would undo it
                        // anyway -- maybe days later, with no reason given.
                        reason = L"The new version started fine, but the update could not be recorded (" + phaseError +
                                 L"), so it was undone to keep things consistent.";
                    }
                } else {
                    reason = L"The new version stopped right after starting (exit code " + exitCodeText(child) + L").";
                }
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
                // Nothing is restored and nothing started while it may still
                // run. Nobody else will tell the user, so this does.
                const std::wstring text = reason + L"\n\nThe new version could not be stopped, so the update could not "
                                                   L"be undone yet. Restart Windows, then start limit-nvpstate: it "
                                                   L"finishes undoing the update. If it doesn't start, run "
                                                   L"limit-nvpstate-updater.exe in its folder.";
                CloseHandle(ready);
                updatetxn::writeResult(appDir, text);
                updatetxn::releaseUpdateLock(lock);   // phase stays "verifying": the next start recovers
                MessageBoxW(nullptr, text.c_str(), L"limit-nvpstate updater", MB_ICONWARNING | MB_OK);
                return EXIT_NEEDS_REPAIR;
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

int recover(const std::wstring& appDir, HANDLE waitFor, HANDLE accepted, bool show) {
    if (testFails(L"LIMITNV_TEST_HELPER_EXIT_EARLY", L"recover")) {
        closeIfSet(waitFor);
        closeIfSet(accepted);
        return EXIT_REFUSED;   // test only: the helper dies right after it starts
    }

    // The lock first, then the check for a running copy: a copy that starts
    // after the check sees the lock and gives way.
    HANDLE lock = updatetxn::takeUpdateLock(LOCK_MS);

    if (!lock) {
        const bool byHand = !waitFor;
        closeIfSet(waitFor);
        closeIfSet(accepted);

        if (byHand) {
            MessageBoxW(nullptr, L"An update is being installed right now. Wait a minute, then try again.",
                        L"limit-nvpstate updater", MB_ICONINFORMATION | MB_OK);
        }

        return EXIT_BUSY;
    }

    acknowledge(accepted);

    if (waitFor) {
        const DWORD closed = WaitForSingleObject(waitFor, OLD_EXIT_MS);
        CloseHandle(waitFor);

        if (closed != WAIT_OBJECT_0) {
            updatetxn::releaseUpdateLock(lock);
            MessageBoxW(nullptr, L"limit-nvpstate did not close, so the unfinished update could not be undone. Restart "
                                 L"Windows, then start limit-nvpstate again.",
                        L"limit-nvpstate updater", MB_ICONWARNING | MB_OK);
            return EXIT_REFUSED;
        }
    }

    if (!waitForNoApp(waitFor ? APP_GONE_MS : 0)) {
        updatetxn::releaseUpdateLock(lock);
        MessageBoxW(nullptr, L"Close limit-nvpstate (right-click its tray icon, Exit), then run this again.",
                    L"limit-nvpstate updater", MB_ICONINFORMATION | MB_OK);
        return EXIT_REFUSED;
    }

    int result = 0;

    if (updatetxn::ownsWorkDir(appDir)) {
        Phase        phase   = Phase::Swapping;
        bool         present = false;
        std::wstring error;

        if (!updatetxn::readPhase(appDir, phase, present, error)) {
            // Never guessed: an unreadable state is not "nothing to do".
            std::wstring phaseError;
            updatetxn::writeResult(appDir, error);
            updatetxn::writePhase(appDir, Phase::NeedsRepair, phaseError);
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

        if (options.accepted) {
            CloseHandle(options.accepted);
        }

        return EXIT_REFUSED;   // the program, if it started this, sees no "accepted" and says so
    }

    return options.mode == Options::Mode::Install ? install(appDir, options.waitFor, options.accepted, options.show)
                                                  : recover(appDir, options.waitFor, options.accepted, options.show);
}
