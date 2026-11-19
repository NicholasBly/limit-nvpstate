#include <limitnvpstate.h>

#include <config.h>
#include <updater.h>
#include <testhooks.h>
#include <updatetxn.h>
#include <utils.h>
#include <version.h>

#include <QApplication>
#include <QIcon>
#include <QWinEventNotifier>

#include <Windows.h>
#include <shellapi.h>

#include <cstdint>
#include <cwchar>

namespace {

// A second launch signals this instead of hunting for the window by title.
// FindWindow("limit-nvpstate") also matches an Explorer window opened on a
// folder of that name, finds nothing at all while the window has never been
// shown (Start Minimized), and ShowWindow() on it unhides the window behind
// Qt's back so Qt still thinks it is hidden.
constexpr wchar_t SHOW_EVENT_NAME[] = L"Local\\limit-nvpstate-show";

// Set by the updater helper when it starts a new version:
// "--after-update <ready event handle> [--show]". The handle is one the helper
// let us inherit; anything that doesn't check out is ignored (a normal start).
struct LaunchOptions {
    HANDLE ready     = nullptr;
    bool   show      = false;
    bool   recovered = false;   // started by the helper right after it tried
};

HANDLE inheritedHandle(const wchar_t* text) {
    wchar_t*                 end   = nullptr;
    const unsigned long long value = wcstoull(text, &end, 10);
    DWORD                    flags = 0;

    if (!end || *end != L'\0' || value == 0) {
        return nullptr;
    }

    const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value));
    return GetHandleInformation(handle, &flags) ? handle : nullptr;
}

LaunchOptions parseLaunchOptions() {
    LaunchOptions options;
    int           count = 0;
    LPWSTR*       args  = CommandLineToArgvW(GetCommandLineW(), &count);

    if (!args) {
        return options;
    }

    for (int i = 1; i < count; i++) {
        if (wcscmp(args[i], L"--after-update") == 0 && i + 1 < count) {
            options.ready = inheritedHandle(args[++i]);
        } else if (wcscmp(args[i], L"--show") == 0) {
            options.show = true;
        } else if (wcscmp(args[i], L"--recovered") == 0) {
            options.recovered = true;
        }
    }

    LocalFree(args);

    // Must be a waitable object (the helper's event).
    if (options.ready && WaitForSingleObject(options.ready, 0) == WAIT_FAILED) {
        options.ready = nullptr;
    }

    return options;
}

// ---- single instance ----
// Brings the running copy's window up, if one is running.
void showRunningCopy() {
    const HANDLE showEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, SHOW_EVENT_NAME);

    if (showEvent) {
        // We are the foreground process (the user just launched us), so we
        // may hand the foreground right to the running instance.
        AllowSetForegroundWindow(ASFW_ANY);
        SetEvent(showEvent);
        CloseHandle(showEvent);
    }
}

// The handle is intentionally left open for the process lifetime; Windows
// releases it on exit. GetLastError() is read immediately, before anything
// else can clobber it. False: another instance runs (and was asked to show).
// `retryMs`: keep trying that long -- for the new version the updater starts,
// in case a copy started by hand holds the mutex for the moment it takes to
// see the update lock and give way.
bool becomeSingleInstance(DWORD retryMs) {
    for (DWORD waited = 0;; waited += 100) {
        const HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\limit-nvpstate");
        const DWORD  mutexError    = GetLastError();

        if (instanceMutex && mutexError != ERROR_ALREADY_EXISTS) {
            break;
        }

        if (instanceMutex) {
            CloseHandle(instanceMutex);
        }

        if (waited >= retryMs) {
            showRunningCopy();
            return false;
        }

        Sleep(100);
    }

    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    const LaunchOptions launch      = parseLaunchOptions();
    const bool          afterUpdate = launch.ready != nullptr;
    const std::wstring  appDir      = getProgramDirectory();

    // The single-instance mutex first, then the update lock -- in this order.
    // The updater takes its lock before the running copy closes (and lets go
    // of the mutex), so a copy that gets the mutex later always sees the lock.
    // The other way round, a copy held up between the two checks could slip
    // in while the files are being swapped.
    if (!becomeSingleInstance(afterUpdate ? 5000 : 0)) {
        return 0;
    }

    testPause(L"LIMITNV_TEST_PAUSE_AT_START_CHECKS");   // test only: between the two start checks

    // The updater helper is at work (swapping files, or deciding about a new
    // version it started): it starts the program itself when it is done. Only
    // the new version it starts gets past this. Exiting lets go of the mutex.
    if (!afterUpdate && updatetxn::updateLockHeld()) {
        testPause(L"LIMITNV_TEST_PAUSE_BEFORE_GIVING_WAY");   // test only: slow to close
        return 0;
    }

    // An update the helper could not finish (a crash, a power cut): it is
    // started again to undo it, and starts the program afterwards. This copy
    // may be running a mix of old and new files, so it stops here, before
    // touching config or the GPU -- also when the undo itself could not
    // finish: then it says what to do, and stops.
    if (!afterUpdate) {
        std::wstring                repairMessage;
        const updater::StartupState startup = updater::checkAtStartup(appDir, launch.recovered, repairMessage);

        if (startup == updater::StartupState::Recovering) {
            return 0;
        }

        if (startup == updater::StartupState::NeedsRepair) {
            const std::wstring text =
                repairMessage + L"\n\nlimit-nvpstate won't run until this is fixed, so it is not managing the GPU now. "
                                L"Start limit-nvpstate again to retry. If that keeps failing: download limit-nvpstate "
                                L"again, unzip it over this folder, then delete the limit-nvpstate-update folder.";
            MessageBoxW(nullptr, text.c_str(), L"limit-nvpstate", MB_ICONERROR | MB_OK);
            return 1;
        }
    }

    // ---- config ----
    // Paths are derived from the module path. The original chdir'd with
    // std::filesystem::current_path(), which is process-global state: it also
    // changed where QFileDialog opens, and threw an unhandled exception when
    // getBasePath() returned an empty string.
    if (!initConfig(appDir)) {
        // /SUBSYSTEM:WINDOWS means there is no console. The original wrote this
        // to std::cerr, so a failed config load produced no visible output at
        // all and the program simply never appeared.
        MessageBoxW(nullptr,
                    L"Failed to initialise config.json.\n\n"
                    L"Check that the program directory is writable.",
                    L"limit-nvpstate",
                    MB_ICONERROR | MB_OK);
        return 1;
    }

    if (afterUpdate && testFails(L"LIMITNV_TEST_TOUCH_CONFIG", L"1")) {
        config["test_touched"] = true;   // test only: a new version that changes your settings
        saveConfig();
    }

    // Qt's fallback session management closes every visible window at logoff
    // and cancels the logoff if one refuses -- and ours always refuses, because
    // closing hides to the tray. So an open window blocked logoff. Accepting
    // that close instead is no better: Qt then destroys the native window, and
    // WM_ENDSESSION, which releases the limits, never arrives. Turn it off.
    // (Deprecated in Qt 5.15, gone in Qt 6 along with the behaviour itself.)
    QGuiApplication::setFallbackSessionManagementEnabled(false);

    QApplication a(argc, argv);
    QCoreApplication::setApplicationVersion(QString::fromLatin1(APP_VERSION_STRING));

    // This is a tray application. Without this, closing the About dialog while
    // the main window is hidden counts as "last window closed" and quits.
    a.setQuitOnLastWindowClosed(false);

    // Every window (About, message boxes) inherits this. PNG rather than ICO:
    // Qt decodes PNG itself, ICO needs the imageformats plugin build.ps1 prunes.
    a.setWindowIcon(QIcon(":/limitnvpstate/icon.png"));

    limitnvpstate w;

    // Initialisation that can fail is no longer done in the constructor, so a
    // failure returns here instead of calling exit() mid-construction. (A new
    // version that fails here never says it is up: the helper rolls back.)
    if (!w.init()) {
        return 1;
    }

    if (afterUpdate) {
        // While the helper is still checking this new version, its settings
        // can't be changed: it may be undone in a moment, and its changes
        // (another GPU, say) with it. finishUpdateLater() unlocks them.
        w.setSettingsLocked(true);
        w.signalReadyLater(launch.ready);
    }

    w.finishUpdateLater();

    // Auto-reset: each SetEvent from a second launch wakes us exactly once.
    // Created only after init() succeeded, so a launch during a failed start
    // finds nothing to signal and just exits.
    const HANDLE showEvent = CreateEventW(nullptr, FALSE, FALSE, SHOW_EVENT_NAME);

    if (showEvent) {
        QWinEventNotifier* notifier = new QWinEventNotifier(showEvent, &w);
        QObject::connect(notifier, &QWinEventNotifier::activated, &w, [&w]() { w.bringToFront(); });
    }

    if (launch.show || !config["start_minimized"].get<bool>()) {
        w.show();
    }

    const int code = a.exec();
    testPause(L"LIMITNV_TEST_PAUSE_AT_EXIT");   // test only: a copy slow to close
    return code;
}
