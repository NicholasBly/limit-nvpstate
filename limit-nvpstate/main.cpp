#include <limitnvpstate.h>

#include <config.h>
#include <updater.h>
#include <utils.h>
#include <version.h>

#include <QApplication>
#include <QIcon>
#include <QWinEventNotifier>

#include <Windows.h>
#include <shellapi.h>

#include <cwchar>

namespace {

// A second launch signals this instead of hunting for the window by title.
// FindWindow("limit-nvpstate") also matches an Explorer window opened on a
// folder of that name, finds nothing at all while the window has never been
// shown (Start Minimized), and ShowWindow() on it unhides the window behind
// Qt's back so Qt still thinks it is hidden.
constexpr wchar_t SHOW_EVENT_NAME[] = L"Local\\limit-nvpstate-show";

// Set by an update: "--after-update <pid of the old instance> [--show]".
struct LaunchOptions {
    DWORD afterUpdatePid = 0;
    bool  show           = false;
};

LaunchOptions parseLaunchOptions() {
    LaunchOptions options;
    int           count = 0;
    LPWSTR*       args  = CommandLineToArgvW(GetCommandLineW(), &count);

    if (!args) {
        return options;
    }

    for (int i = 1; i < count; i++) {
        if (wcscmp(args[i], L"--after-update") == 0 && i + 1 < count) {
            options.afterUpdatePid = static_cast<DWORD>(wcstoul(args[++i], nullptr, 10));
        } else if (wcscmp(args[i], L"--show") == 0) {
            options.show = true;
        }
    }

    LocalFree(args);
    return options;
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    const LaunchOptions launch = parseLaunchOptions();

    // Started by an update: the old instance still holds the single-instance
    // mutex for a moment. Wait for it to exit (bounded) before taking it.
    if (launch.afterUpdatePid != 0) {
        const HANDLE oldInstance = OpenProcess(SYNCHRONIZE, FALSE, launch.afterUpdatePid);

        if (oldInstance) {
            WaitForSingleObject(oldInstance, 15000);
            CloseHandle(oldInstance);
        }
    }

    // ---- single instance ----
    // The handle is intentionally left open for the process lifetime; Windows
    // releases it on exit. GetLastError() is read immediately, before anything
    // else can clobber it.
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

        return 0;
    }

    // ---- config ----
    // Paths are derived from the module path. The original chdir'd with
    // std::filesystem::current_path(), which is process-global state: it also
    // changed where QFileDialog opens, and threw an unhandled exception when
    // getBasePath() returned an empty string.
    if (!initConfig(getProgramDirectory())) {
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
    // failure returns here instead of calling exit() mid-construction.
    if (!w.init()) {
        return 1;
    }

    // Auto-reset: each SetEvent from a second launch wakes us exactly once.
    // Created only after init() succeeded, so a launch during a failed start
    // finds nothing to signal and just exits.
    const HANDLE showEvent = CreateEventW(nullptr, FALSE, FALSE, SHOW_EVENT_NAME);

    if (showEvent) {
        QWinEventNotifier* notifier = new QWinEventNotifier(showEvent, &w);
        QObject::connect(notifier, &QWinEventNotifier::activated, &w, [&w]() { w.bringToFront(); });
    }

    // Files an earlier update set aside ("*.old") are no longer in use now.
    updater::removeLeftovers(getProgramDirectory());

    if (launch.afterUpdatePid != 0) {
        w.showUpdatedNotice();
    }

    if (launch.show || !config["start_minimized"].get<bool>()) {
        w.show();
    }

    return a.exec();
}
