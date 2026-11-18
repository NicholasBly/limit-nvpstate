#include <limitnvpstate.h>
#include <testhooks.h>

#include <config.h>
#include <updater.h>
#include <utils.h>
#include <version.h>

#include <QApplication>
#include <QMessageBox>
#include <QThread>
#include <QTimer>
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

// Set by an update: "--after-update <old process handle> <ready event handle>
// [--show]". Both are handles the old instance let us inherit; anything that
// doesn't check out as such is ignored (a normal start).
struct LaunchOptions {
    HANDLE oldInstance = nullptr;
    HANDLE ready       = nullptr;
    bool   show        = false;
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
        if (wcscmp(args[i], L"--after-update") == 0 && i + 2 < count) {
            options.oldInstance = inheritedHandle(args[++i]);
            options.ready       = inheritedHandle(args[++i]);
        } else if (wcscmp(args[i], L"--show") == 0) {
            options.show = true;
        }
    }

    LocalFree(args);

    // The old instance must be a process and the event a waitable object.
    if (!options.oldInstance || !options.ready || GetProcessId(options.oldInstance) == 0
        || WaitForSingleObject(options.ready, 0) == WAIT_FAILED) {
        options.oldInstance = nullptr;
        options.ready       = nullptr;
    }

    return options;
}

// ---- single instance ----
// The handle is intentionally left open for the process lifetime; Windows
// releases it on exit. GetLastError() is read immediately, before anything
// else can clobber it. False: another instance runs (and was asked to show).
bool becomeSingleInstance() {
    const HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\limit-nvpstate");
    const DWORD  mutexError    = GetLastError();

    if (!instanceMutex || mutexError == ERROR_ALREADY_EXISTS) {
        const HANDLE showEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, SHOW_EVENT_NAME);

        if (showEvent) {
            // We are the foreground process (the user just launched us), so we
            // may hand the foreground right to the running instance.
            AllowSetForegroundWindow(ASFW_ANY);
            SetEvent(showEvent);
            CloseHandle(showEvent);
        }

        return false;
    }

    return true;
}

// Off the GUI thread: deleting the backups may wait out a lock for a moment.
void finishUpdateInBackground(const std::wstring& appDir) {
    QThread* thread = QThread::create([appDir]() { updater::finishUpdate(appDir); });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void showPlainWarning(const QString& text) {
    QMessageBox box(QMessageBox::Warning, QStringLiteral("limit-nvpstate"), text);
    box.setTextFormat(Qt::PlainText);
    box.exec();
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    const LaunchOptions launch      = parseLaunchOptions();
    const bool          afterUpdate = launch.oldInstance != nullptr;
    const std::wstring  appDir      = getProgramDirectory();

    updater::Recovery recovery = updater::Recovery::None;
    std::wstring      recoveryError;

    // Started by an update, the old instance still holds the single-instance
    // mutex: it waits for us to say we're up, then exits, and only then do we
    // take the mutex. A normal start takes it first, then deals with anything
    // an earlier update left behind.
    if (!afterUpdate) {
        if (!becomeSingleInstance()) {
            return 0;
        }

        recovery = updater::recoverAtStartup(appDir, recoveryError);
    }

    // This process is the new version of an update that was never confirmed.
    // If it can't start, put the old version back so the next start works.
    const auto failedToStart = [&]() {
        if (recovery == updater::Recovery::PendingFinish) {
            std::wstring error;
            const bool   undone = updater::undoInstall(appDir, error);

            MessageBoxW(nullptr,
                        undone ? L"The new version could not start, so the update was undone. Start limit-nvpstate "
                                 L"again to use the previous version."
                               : (L"The new version could not start, and the update could not be fully undone.\n\n"
                                  + error).c_str(),
                        L"limit-nvpstate", MB_ICONERROR | MB_OK);
        }

        return 1;
    };

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
        return failedToStart();
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
    // failure returns here instead of calling exit() mid-construction. (After
    // an update, the old instance sees us exit and undoes the update.)
    if (!w.init()) {
        return failedToStart();
    }

    if (afterUpdate) {
        // Up: tell the old instance, which then exits. The old version had
        // already released the GPU and stopped watching before starting us.
        testPause(L"LIMITNV_TEST_PAUSE_BEFORE_READY");
        SetEvent(launch.ready);
        CloseHandle(launch.ready);
        WaitForSingleObject(launch.oldInstance, 30000);
        CloseHandle(launch.oldInstance);

        if (!becomeSingleInstance()) {
            // Another copy started in between and manages the GPU now; leave
            // it alone rather than releasing it on the way out.
            w.yieldGpu();
            return 0;
        }
    }

    if (afterUpdate || recovery == updater::Recovery::PendingFinish) {
        finishUpdateInBackground(appDir);   // final: the backups can go
    }

    // Auto-reset: each SetEvent from a second launch wakes us exactly once.
    // Created only after init() succeeded, so a launch during a failed start
    // finds nothing to signal and just exits.
    const HANDLE showEvent = CreateEventW(nullptr, FALSE, FALSE, SHOW_EVENT_NAME);

    if (showEvent) {
        QWinEventNotifier* notifier = new QWinEventNotifier(showEvent, &w);
        QObject::connect(notifier, &QWinEventNotifier::activated, &w, [&w]() { w.bringToFront(); });
    }

    if (afterUpdate) {
        w.showUpdatedNotice();
    }

    if (launch.show || !config["start_minimized"].get<bool>()) {
        w.show();
    }

    if (recovery == updater::Recovery::Undone) {
        QTimer::singleShot(0, &w, []() {
            showPlainWarning(QStringLiteral("An earlier update did not finish, so it was undone. You are on "
                                            "version %1.").arg(QString::fromLatin1(APP_VERSION_STRING)));
        });
    } else if (recovery == updater::Recovery::NeedsRepair) {
        const QString error = QString::fromStdWString(recoveryError);

        QTimer::singleShot(0, &w, [error]() {
            showPlainWarning(QStringLiteral("An earlier update did not finish, and it could not be fully undone.\n\n"
                                            "%1\n\nIf limit-nvpstate misbehaves, download it again from:\n%2")
                                 .arg(error, QString::fromWCharArray(updater::releasesPageUrl())));
        });
    }

    return a.exec();
}
