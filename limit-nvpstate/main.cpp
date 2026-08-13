#include <limitnvpstate.h>

#include <config.h>
#include <utils.h>

#include <QApplication>

#include <Windows.h>

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    // ---- single instance ----
    // The handle is intentionally left open for the process lifetime; Windows
    // releases it on exit. GetLastError() is read immediately, before anything
    // else can clobber it.
    const HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\limit-nvpstate");
    const DWORD  mutexError    = GetLastError();

    if (!instanceMutex || mutexError == ERROR_ALREADY_EXISTS) {
        HWND hWnd = FindWindowW(nullptr, L"limit-nvpstate");

        if (hWnd) {
            // The running instance is normally hidden in the tray, and
            // SetForegroundWindow on a hidden window does nothing. SW_RESTORE
            // both unhides and un-minimises it.
            ShowWindow(hWnd, SW_RESTORE);
            SetForegroundWindow(hWnd);
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

    QApplication a(argc, argv);

    // This is a tray application. Without this, closing the About dialog while
    // the main window is hidden counts as "last window closed" and quits.
    a.setQuitOnLastWindowClosed(false);

    limitnvpstate w;

    // Initialisation that can fail is no longer done in the constructor, so a
    // failure returns here instead of calling exit() mid-construction.
    if (!w.init()) {
        return 1;
    }

    if (!config["start_minimized"].get<bool>()) {
        w.show();
    }

    return a.exec();
}
