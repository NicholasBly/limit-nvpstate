#include <limitnvpstate.h>
#include <testhooks.h>

#include <about.h>
#include <config.h>
#include <log.h>
#include <nvidia.h>
#include <pstatepolicy.h>
#include <updater.h>
#include <updatetxn.h>
#include <updaterules.h>
#include <utils.h>
#include <version.h>

#include <QAbstractNativeEventFilter>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>

#include <tlhelp32.h>

// Depending on SDK version and WIN32_LEAN_AND_MEAN these may or may not be
// pulled in by winuser.h. Defining them defensively costs nothing.
#ifndef OBJID_WINDOW
#define OBJID_WINDOW ((LONG)0x00000000)
#endif
#ifndef CHILDID_SELF
#define CHILDID_SELF 0
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// ===========================================================================
// How a limit gets applied
//
// Three things can change what the GPU should be set to: the process poll
// (worker thread), the foreground hook (GUI thread) and the settings (GUI
// thread). All of them write into one SharedState under one lock and then call
// evaluateLocked(), which asks policy::computeTarget() for the answer and
// applies it -- inside the same lock. So two threads can never compute
// different answers and apply them in the wrong order, which the previous
// "check a flag, then call NVAPI" design allowed.
// ===========================================================================

namespace {

constexpr int TRIGGER_PROCESS_RUNNING    = 0;
constexpr int TRIGGER_PROCESS_FOREGROUND = 1;

const char* const UNLIMIT_TRIGGERS[] = { "Process Running", "Process Foreground" };
constexpr int     UNLIMIT_TRIGGER_COUNT = static_cast<int>(std::size(UNLIMIT_TRIGGERS));

// What `applied` holds for a GPU we have never sent anything to, and for one
// whose last NVAPI call failed (state unknown, so always retry and release).
constexpr int APPLIED_NEVER   = -1;
constexpr int APPLIED_UNKNOWN = -2;

const char* const BACKGROUND_TOOLTIP =
    "Overrides the default \"Limit P-State\" setting to set limits per-app.\n"
    "Applies while the app is open but not in front; in front it is always unlimited.";

// Longest the scan ever waits between full re-checks, even with nothing to gain.
constexpr DWORD WATCHDOG_MS = 60000;

// WaitForMultipleObjects takes 64 handles; two are the stop and wake events.
constexpr size_t MAX_WAIT_PROCESSES = MAXIMUM_WAIT_OBJECTS - 2;

NvPhysicalGpuHandle hPhysicalGpus[NVAPI_MAX_PHYSICAL_GPUS] = {};
NvU32               gpuCount = 0;

struct ProcessEntry {
    std::wstring name;
    int          background = policy::BACKGROUND_DEFAULT;
    bool         running    = false;   // written by the poll thread
};

struct SharedState {
    std::vector<ProcessEntry> entries;
    std::wstring              foregroundName;   // exe owning the foreground window, "" if unknown
    bool                      pollActive   = false;
    bool                      hookActive   = false;
    policy::Trigger           trigger      = policy::Trigger::ProcessRunning;
    int                       gpuIndex     = 0;
    int                       globalLimit  = 8;
    DWORD                     pollMs       = 5000;
    bool                      shuttingDown = false;
    int                       applied[NVAPI_MAX_PHYSICAL_GPUS];

    // The most recent driver refusal, for error messages.
    struct {
        int gpu    = -1;
        int target = 0;
        int status = 0;
    } lastFailure;

    std::vector<policy::EntryState> scratch;    // reused by evaluateLocked()

    SharedState() {
        for (int& value : applied) {
            value = APPLIED_NEVER;
        }
    }
};

SRWLOCK     stateLock = SRWLOCK_INIT;
SharedState state;

class StateGuard {
public:
    StateGuard()  { AcquireSRWLockExclusive(&stateLock); }
    ~StateGuard() { ReleaseSRWLockExclusive(&stateLock); }

    StateGuard(const StateGuard&)            = delete;
    StateGuard& operator=(const StateGuard&) = delete;
};

// Foreground trigger
HWINEVENTHOOK      eventHook = nullptr;
std::atomic<DWORD> lastForegroundPid{ 0 };

// Process-running trigger
HANDLE pollThread    = nullptr;
HANDLE pollStopEvent = nullptr;   // manual-reset: "shut down"
HANDLE pollWakeEvent = nullptr;   // auto-reset:   "rescan now"

std::atomic<bool> fatalReported{ false };

// The program is on its way out (Exit, logoff, fatal error). Unlike
// state.shuttingDown -- which a restart into an update also sets, and undoes
// if the new version fails -- this never goes back to false.
std::atomic<bool> exiting{ false };

void beginExit() {
    exiting = true;

    StateGuard guard;
    state.shuttingDown = true;   // nothing in flight may apply a new limit
}

// Windows' own case-insensitive file-name comparison. _wcsicmp only folds
// ASCII in the C locale, so "Ärger.exe" and "ärger.exe" did not match.
bool sameName(const std::wstring& a, const wchar_t* b) {
    return CompareStringOrdinal(a.c_str(), -1, b, -1, TRUE) == CSTR_EQUAL;
}

// ---- must hold stateLock ---------------------------------------------------

bool applyLocked(int gpu, int target) {
    if (gpu < 0 || static_cast<NvU32>(gpu) >= gpuCount) {
        return false;
    }

    if (state.applied[gpu] == target) {
        return true;
    }

    const int status = setPStateLimit(hPhysicalGpus[gpu], static_cast<unsigned int>(target));

    if (status != NVAPI_OK) {
        state.applied[gpu]       = APPLIED_UNKNOWN;
        state.lastFailure.gpu    = gpu;
        state.lastFailure.target = target;
        state.lastFailure.status = status;
        return false;
    }

    state.applied[gpu] = target;
    return true;
}

bool evaluateLocked() {
    if (state.shuttingDown) {
        return true;
    }

    state.scratch.clear();

    for (const ProcessEntry& entry : state.entries) {
        const bool inFront = state.hookActive
                          && !state.foregroundName.empty()
                          && sameName(entry.name, state.foregroundName.c_str());

        state.scratch.push_back({ entry.background, state.pollActive && entry.running, inFront });
    }

    const int target = policy::computeTarget(state.scratch, state.trigger, state.globalLimit);

    return applyLocked(state.gpuIndex, target);
}

bool isListedLocked(const wchar_t* processName) {
    for (const ProcessEntry& entry : state.entries) {
        if (sameName(entry.name, processName)) {
            return true;
        }
    }

    return false;
}

// Returns the GPUs that could not be released (driver refused).
std::vector<int> releaseAllLocked() {
    std::vector<int> failed;

    for (NvU32 i = 0; i < gpuCount; i++) {
        if (state.applied[i] != APPLIED_NEVER && state.applied[i] != policy::PSTATE_UNLIMITED) {
            if (!applyLocked(static_cast<int>(i), policy::PSTATE_UNLIMITED)) {
                failed.push_back(static_cast<int>(i));
            }
        }
    }

    return failed;
}

// "GPU 0, P8, NVAPI status -1" for the most recent refusal.
// Caller holds stateLock.
QString failureTextLocked() {
    return QStringLiteral("GPU %1, %2, NVAPI status %3")
        .arg(state.lastFailure.gpu)
        .arg(state.lastFailure.target == policy::PSTATE_UNLIMITED ? QStringLiteral("release")
                                                                   : QStringLiteral("P%1").arg(state.lastFailure.target))
        .arg(state.lastFailure.status);
}

QString lastFailureText() {
    StateGuard guard;
    return failureTextLocked();
}

// ---------------------------------------------------------------------------

bool evaluate() {
    StateGuard guard;
    return evaluateLocked();
}

void stopAllTriggers();

// Any thread. Shows one error, ever: the first failure stops the detectors
// before the message box opens, so the box taking the foreground cannot
// trigger another failure and another box. A QMessageBox may only be built on
// the GUI thread, so the work is queued there.
void reportFatal(const QString& message) {
    if (fatalReported.exchange(true)) {
        return;
    }

    QMetaObject::invokeMethod(qApp, [message]() {
        // No more limits from here on, then stop and release.
        beginExit();
        stopAllTriggers();

        {
            StateGuard guard;
            releaseAllLocked();
        }

        QMessageBox::critical(nullptr, "limit-nvpstate", message);
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);
}

// A driver call failed: say which GPU, what was asked and what NVAPI said.
void reportDriverFailure() {
    reportFatal(QStringLiteral("Error: Failed to set P-State (%1)").arg(lastFailureText()));
}

void wakePoll() {
    if (pollWakeEvent) {
        SetEvent(pollWakeEvent);
    }
}

// ---------------------------------------------------------------------------
// Foreground trigger
// ---------------------------------------------------------------------------

// GUI thread. `force` re-evaluates even if the same process is still in front
// (used right after the hook is installed).
void onForegroundWindow(HWND hwnd, bool force) {
    if (!hwnd) {
        return;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    if (pid == 0) {
        return;
    }

    // Alt-tabbing between two windows of the same process, or a dialog opening
    // over its parent, needs no work at all.
    if (lastForegroundPid.exchange(pid, std::memory_order_relaxed) == pid && !force) {
        return;
    }

    wchar_t processName[MAX_PATH];

    if (!getProcessNameByPID(pid, processName, MAX_PATH)) {
        // Unknown counts as "not a listed exe". Keeping the old name instead
        // would leave a game marked as in front after it lost the foreground.
        // Don't remember this pid either: a failed lookup is not an answer,
        // and the next event for the same process should try again.
        LOG(L"warn: could not resolve a name for pid %lu\n", pid);
        processName[0] = L'\0';
        lastForegroundPid.store(0, std::memory_order_relaxed);
    }

    bool ok          = true;
    bool wakeTheScan = false;

    {
        StateGuard guard;

        state.foregroundName = processName;
        ok                   = evaluateLocked();

        // A listed exe that just came to the front may have started after the
        // last scan. Rescan now, so when it goes behind its "running" flag is
        // already set instead of arriving up to one poll interval late.
        wakeTheScan = state.pollActive && processName[0] && isListedLocked(processName);
    }

    LOG(L"info: %s is fg\n", processName);

    if (wakeTheScan) {
        wakePoll();
    }

    if (!ok) {
        reportDriverFailure();
    }
}

void CALLBACK winEventProc(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    // WINEVENT_OUTOFCONTEXT delivers this on the message loop of the thread that
    // installed the hook, i.e. the GUI thread.

    // EVENT_SYSTEM_FOREGROUND also arrives for child objects. Filter to the
    // top-level window transition and use the hwnd the hook gave us.
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) {
        return;
    }

    onForegroundWindow(hwnd ? hwnd : GetForegroundWindow(), false);
}

bool startForegroundHook() {
    if (eventHook) {
        return true;
    }

    eventHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND,
                                EVENT_SYSTEM_FOREGROUND,
                                nullptr,
                                winEventProc,
                                0,
                                0,
                                WINEVENT_OUTOFCONTEXT);

    if (!eventHook) {
        return false;
    }

    {
        StateGuard guard;
        state.hookActive = true;
    }

    // Evaluate whatever is in front right now instead of waiting for the next
    // window switch, so launching while a game is in front unlocks at once.
    onForegroundWindow(GetForegroundWindow(), true);

    return true;
}

void stopForegroundHook() {
    if (!eventHook) {
        return;
    }

    UnhookWinEvent(eventHook);
    eventHook = nullptr;
    lastForegroundPid.store(0, std::memory_order_relaxed);

    StateGuard guard;
    state.hookActive = false;
    state.foregroundName.clear();
}

// ---------------------------------------------------------------------------
// Process-running trigger
// ---------------------------------------------------------------------------

DWORD WINAPI pollProcesses(LPVOID) {
    // Normal priority on purpose. This thread sleeps nearly all the time, but
    // it takes stateLock, and SRW locks have no priority inheritance: at
    // below-normal priority, a game saturating every core could preempt it
    // while it held the lock and stall the foreground hook (the alt-tab back
    // into the game) for seconds.

    std::vector<std::wstring> names;         // copy of the list, matched without the lock
    std::vector<char>         seen;          // per name: found in this snapshot
    std::vector<DWORD>        matchedPids;
    std::vector<HANDLE>       waits;

    matchedPids.reserve(MAX_WAIT_PROCESSES);
    waits.reserve(MAXIMUM_WAIT_OBJECTS);

    for (;;) {
        matchedPids.clear();

        bool  ok               = true;
        bool  truncated        = false;
        bool  unlimitedRunning = false;
        DWORD interval         = 5000;

        {
            StateGuard guard;

            names.clear();

            for (const ProcessEntry& entry : state.entries) {
                names.push_back(entry.name);
            }
        }

        seen.assign(names.size(), 0);

        // CreateToolhelp32Snapshot returns INVALID_HANDLE_VALUE on failure, not
        // NULL. The walk happens without the lock: it is the slow part.
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

        // Only a walk that ends with ERROR_NO_MORE_FILES saw every process. A
        // walk that broke off partway is not "those games exited": acting on
        // it would clamp a game that is still running.
        bool complete = false;

        if (hSnapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W processEntry = {};
            processEntry.dwSize          = sizeof(processEntry);

            if (Process32FirstW(hSnapshot, &processEntry)) {
                do {
                    for (size_t i = 0; i < names.size(); i++) {
                        if (!sameName(names[i], processEntry.szExeFile)) {
                            continue;
                        }

                        seen[i] = 1;

                        if (matchedPids.size() < MAX_WAIT_PROCESSES) {
                            matchedPids.push_back(processEntry.th32ProcessID);
                        } else {
                            truncated = true;
                        }

                        break;
                    }
                } while (Process32NextW(hSnapshot, &processEntry));

                complete = GetLastError() == ERROR_NO_MORE_FILES;   // read before CloseHandle
            }

            if (!complete) {
                LOG(L"error: process walk broke off (%lu)\n", GetLastError());
            }

            CloseHandle(hSnapshot);
        } else {
            LOG(L"error: CreateToolhelp32Snapshot failed (%lu)\n", GetLastError());
        }

        {
            StateGuard guard;

            // Matched by name, so a list edited during the walk is fine: a new
            // name reads as "not running" until the wake the edit sends. A
            // failed or incomplete walk keeps the last known flags rather than
            // pretending everything just exited.
            if (complete) {
                for (ProcessEntry& entry : state.entries) {
                    entry.running = false;

                    for (size_t i = 0; i < names.size(); i++) {
                        if (seen[i] && sameName(names[i], entry.name.c_str())) {
                            entry.running = true;
                            break;
                        }
                    }
                }
            }

            ok               = evaluateLocked();
            unlimitedRunning = policy::runningContributesUnlimited(state.scratch, state.trigger);
            interval         = state.pollMs;
        }

        if (!ok) {
            reportDriverFailure();
            return 1;
        }

        // Wait for any matched process to exit (rescan straight away), a wake
        // request (list or trigger changed) or shutdown. If something running
        // already counts as Unlimited, nothing that starts could do better, so
        // rescanning every poll interval is pointless -- that only holds if we
        // can see every exit, though. Even then, re-check once a minute: a pid
        // can exit and be reused between the snapshot and OpenProcess, and the
        // wait would then be on some unrelated process.
        waits.clear();
        waits.push_back(pollStopEvent);
        waits.push_back(pollWakeEvent);

        // An incomplete walk may have missed instances: retry on the timer.
        bool watchingAll = !truncated && complete;

        for (DWORD pid : matchedPids) {
            HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);

            if (!hProcess) {
                LOG(L"warn: could not open pid %lu for synchronisation\n", pid);
                watchingAll = false;
                continue;
            }

            // Already exited but still listed (another program holds a handle
            // to it): waiting on it would wake at once, every time -- a busy
            // loop. Leave it to the timer.
            if (WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0) {
                CloseHandle(hProcess);
                watchingAll = false;
                continue;
            }

            waits.push_back(hProcess);
        }

        const DWORD timeout = (unlimitedRunning && watchingAll && waits.size() > 2) ? WATCHDOG_MS : interval;
        const DWORD result  = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, timeout);
        const DWORD waitError = result == WAIT_FAILED ? GetLastError() : 0;   // before CloseHandle changes it

        for (size_t i = 2; i < waits.size(); i++) {
            CloseHandle(waits[i]);
        }

        if (result == WAIT_OBJECT_0) {
            return 0;
        }

        if (result == WAIT_FAILED) {
            // Never spin: fall back to a plain interval.
            LOG(L"error: WaitForMultipleObjects failed (%lu)\n", waitError);
            (void)waitError;   // LOG compiles to nothing in Release

            if (WaitForSingleObject(pollStopEvent, interval) == WAIT_OBJECT_0) {
                return 0;
            }
        }
    }
}

bool startPollThread() {
    if (pollThread) {
        return true;
    }

    if (!pollStopEvent) {
        pollStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }

    if (!pollWakeEvent) {
        pollWakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }

    if (!pollStopEvent || !pollWakeEvent) {
        return false;
    }

    ResetEvent(pollStopEvent);

    {
        StateGuard guard;
        state.pollActive = true;
    }

    pollThread = CreateThread(nullptr, 0, pollProcesses, nullptr, 0, nullptr);

    if (!pollThread) {
        StateGuard guard;
        state.pollActive = false;
        return false;
    }

    return true;
}

// Never call with stateLock held: the thread may be waiting for it.
void stopPollThread() {
    if (!pollThread) {
        return;
    }

    SetEvent(pollStopEvent);

    // A real join. The thread only ever blocks on the stop event, the lock or
    // a single NVAPI call, so this returns promptly; giving up after a timeout
    // would let a second thread start alongside a live first one.
    WaitForSingleObject(pollThread, INFINITE);

    CloseHandle(pollThread);
    pollThread = nullptr;

    StateGuard guard;
    state.pollActive = false;

    for (ProcessEntry& entry : state.entries) {
        entry.running = false;
    }
}

void stopAllTriggers() {
    stopPollThread();
    stopForegroundHook();
}

// Starts and stops detectors to match the trigger and the list, then
// re-evaluates. GUI thread.
void updateDetectors() {
    policy::Trigger trigger;
    bool            anyOverride = false;
    bool            anyEntries  = false;

    {
        StateGuard guard;

        trigger    = state.trigger;
        anyEntries = !state.entries.empty();

        for (const ProcessEntry& entry : state.entries) {
            anyOverride |= entry.background != policy::BACKGROUND_DEFAULT;
        }
    }

    const policy::Detectors wanted = policy::detectorsFor(trigger, anyOverride, anyEntries);

    if (!wanted.poll) {
        stopPollThread();
    }

    if (!wanted.hook) {
        stopForegroundHook();
    }

    // Without its detector the app cannot tell when a game is running or in
    // front, so it would hold the limit on everything. Release and exit
    // instead (as the original did), with one error.
    if (wanted.hook && !startForegroundHook()) {
        reportFatal(QStringLiteral("Error: Failed to configure the global hook (%1)").arg(GetLastError()));
        return;
    }

    if (wanted.poll && !startPollThread()) {
        reportFatal(QStringLiteral("Error: Failed to start the polling thread (%1)").arg(GetLastError()));
        return;
    }

    if (!evaluate()) {
        reportDriverFailure();
    }
}

// Swaps in a new list, keeping each surviving entry's "running" flag so an
// edit does not briefly forget a game that is still open.
void publishEntries(std::vector<ProcessEntry> updated) {
    StateGuard guard;

    for (ProcessEntry& entry : updated) {
        for (const ProcessEntry& previous : state.entries) {
            if (sameName(previous.name, entry.name.c_str())) {
                entry.running = previous.running;
                break;
            }
        }
    }

    state.entries.swap(updated);
}

// Saves config.json. If that fails (read-only folder, full disk, file locked),
// says so once per session instead of letting every change look saved.
void persist() {
    static bool warned = false;

    if (saveConfig() || warned) {
        return;
    }

    warned = true;
    QMessageBox::warning(nullptr,
                         "limit-nvpstate",
                         "Could not save settings to config.json.\n\n"
                         "Your changes apply now but will be lost when the program closes. "
                         "Check that the program folder is writable.");
}

// The configured limit if this GPU offers it, otherwise its deepest state
// (`available` is sorted ascending, see queryPStates).
int pickGlobalLimit(const std::vector<int>& available, int wanted) {
    for (int pstate : available) {
        if (pstate == wanted) {
            return pstate;
        }
    }

    return available.back();
}

// ---------------------------------------------------------------------------
// Exception list rows (GUI thread)
// ---------------------------------------------------------------------------

int rowBackground(const QTreeWidget* list, QTreeWidgetItem* item) {
    const QComboBox* combo = qobject_cast<QComboBox*>(list->itemWidget(item, 1));

    return combo ? combo->currentData().toInt() : policy::BACKGROUND_DEFAULT;
}

// Reads every row into both config keys (in memory; the caller saves).
// Returns the rows as entries.
std::vector<ProcessEntry> updateProcessConfig(const QTreeWidget* list) {
    nlohmann::json            names     = nlohmann::json::array();
    nlohmann::json            overrides = nlohmann::json::object();
    std::vector<ProcessEntry> entries;

    const int rows = list->topLevelItemCount();
    entries.reserve(static_cast<size_t>(rows));

    for (int i = 0; i < rows; i++) {
        QTreeWidgetItem* item       = list->topLevelItem(i);
        std::wstring     name       = item->text(0).toStdWString();
        const int        background = rowBackground(list, item);

        if (name.empty()) {
            continue;
        }

        const std::string utf8 = wideToUtf8(name.c_str());

        // Names stay a plain string array, exactly as before, so older builds
        // still read this config; they just ignore the overrides object.
        names.push_back(utf8);

        if (background != policy::BACKGROUND_DEFAULT) {
            overrides[utf8] = background;
        }

        entries.push_back({ std::move(name), background, false });
    }

    config["process_exceptions"]         = std::move(names);
    config["process_background_pstates"] = std::move(overrides);

    return entries;
}

// ---------------------------------------------------------------------------
// Check for updates (GUI side helpers)
// ---------------------------------------------------------------------------

// Shared between the GUI and one background check/download. The worker only
// writes this; the GUI reads the result after QThread::finished, and polls the
// progress counters on a timer -- so the worker never touches a widget, and
// quitting mid-download cannot leave it posting to a destroyed window.
struct UpdateJob {
    std::atomic<bool>      cancel{ false };
    std::atomic<long long> received{ 0 };
    std::atomic<long long> total{ -1 };

    bool             ok = false;
    std::wstring     error;
    updater::Release release;
    std::wstring     packageDir;
};

// Runs `work` on a new thread, then `done` on the GUI thread (via `context`).
template <typename Work, typename Done>
void runInBackground(QObject* context, Work work, Done done) {
    QThread* thread = QThread::create(std::move(work));
    QObject::connect(thread, &QThread::finished, context, std::move(done));
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

QProgressDialog* makeProgressDialog(QWidget* parent, const QString& text, int maximum) {
    QProgressDialog* dialog = new QProgressDialog(text, QStringLiteral("Cancel"), 0, maximum, parent);
    dialog->setWindowTitle(QStringLiteral("limit-nvpstate"));
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(0);
    dialog->setAutoClose(false);
    dialog->setAutoReset(false);
    dialog->show();
    return dialog;
}

// Not close(): QProgressDialog::closeEvent emits canceled(), which would read
// as the user pressing Cancel and swallow the result.
void dismissProgressDialog(QProgressDialog* dialog) {
    QObject::disconnect(dialog, &QProgressDialog::canceled, nullptr, nullptr);
    dialog->hide();
    dialog->deleteLater();
}

// Updater messages carry text from the network (versions, links, server
// errors): always plain text, never interpreted as rich text.
void showPlain(QWidget* parent, QMessageBox::Icon icon, const QString& text) {
    QMessageBox box(icon, QStringLiteral("limit-nvpstate"), text, QMessageBox::Ok, parent);
    box.setTextFormat(Qt::PlainText);
    box.exec();
}


bool runningAsLimitNvpstateExe() {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);

    if (n == 0 || n >= MAX_PATH) {
        return false;
    }

    const wchar_t* name = wcsrchr(path, L'\\');
    name                = name ? name + 1 : path;
    return CompareStringOrdinal(name, -1, L"limit-nvpstate.exe", -1, TRUE) == CSTR_EQUAL;
}

// ---------------------------------------------------------------------------
// Session end
// ---------------------------------------------------------------------------

// Windows messages the app reacts to outside Qt.
//
// WM_ENDSESSION: without it, a logoff terminates the process with the limit
// still applied. wParam TRUE means the session really is ending (a
// WM_QUERYENDSESSION alone can still be cancelled by another program).
//
// WM_POWERBROADCAST resume: what we last sent the driver is all we know -- the
// limit cannot be read back -- and it may not survive sleep. Send it again.
class SessionEndFilter : public QAbstractNativeEventFilter {
public:
    explicit SessionEndFilter(limitnvpstate* owner) : owner(owner) {}

    bool nativeEventFilter(const QByteArray& eventType, void* message, long* result) override {
        Q_UNUSED(result);

        if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
            return false;
        }

        const MSG* msg = static_cast<const MSG*>(message);

        if (!msg) {
            return false;
        }

        if (msg->message == WM_ENDSESSION && msg->wParam) {
            beginExit();   // no new limits while stopping
            stopAllTriggers();
            owner->releaseLimits();
        } else if (msg->message == WM_POWERBROADCAST
                   && (msg->wParam == PBT_APMRESUMEAUTOMATIC || msg->wParam == PBT_APMRESUMESUSPEND)) {
            // Both arrive on a user-initiated resume; once is enough.
            const DWORD now = GetTickCount();

            if (now - lastResume < 2000) {
                return false;
            }

            lastResume = now;

            bool ok;

            {
                StateGuard guard;

                if (state.applied[state.gpuIndex] != APPLIED_NEVER) {
                    state.applied[state.gpuIndex] = APPLIED_UNKNOWN;
                }

                ok = evaluateLocked();
            }

            if (!ok) {
                reportDriverFailure();
            }
        }

        return false;
    }

private:
    limitnvpstate* owner;
    DWORD          lastResume = 0;
};

SessionEndFilter* sessionEndFilter = nullptr;

} // namespace

// ===========================================================================
// limitnvpstate
// ===========================================================================

limitnvpstate::limitnvpstate(QWidget* parent) : QMainWindow(parent) {
    // No per-window icon: every window uses the application icon set in main().
    ui.setupUi(this);
}

limitnvpstate::~limitnvpstate() {
    stopAllTriggers();

    // Belt and braces: if init() failed partway through, or the process is
    // unwinding for any other reason, never leave the GPU clamped.
    releaseLimits();

    if (sessionEndFilter) {
        qApp->removeNativeEventFilter(sessionEndFilter);
        delete sessionEndFilter;
        sessionEndFilter = nullptr;
    }

    if (pollStopEvent) {
        CloseHandle(pollStopEvent);
        pollStopEvent = nullptr;
    }

    if (pollWakeEvent) {
        CloseHandle(pollWakeEvent);
        pollWakeEvent = nullptr;
    }
}

bool limitnvpstate::fail(const char* message) {
    QMessageBox::critical(nullptr, "limit-nvpstate", message);
    return false;
}

bool limitnvpstate::init() {
    createTrayIcon();

    if (initNvAPI() != 0) {
        return fail("Error: Failed to initialize NVAPI");
    }

    if (NvAPI_EnumPhysicalGPUs(hPhysicalGpus, &gpuCount) != 0) {
        return fail("Error: Failed to enumerate physical GPUs");
    }

    if (gpuCount == 0) {
        return fail("Error: No NVIDIA GPUs found");
    }

    // ---- File -> Start Minimized ----
    ui.actionStartMinimized->setChecked(config["start_minimized"].get<bool>());

    connect(ui.actionStartMinimized, &QAction::triggered, this, [this]() {
        config["start_minimized"] = ui.actionStartMinimized->isChecked();
        persist();
    });

    // ---- File -> Add To Startup ----
    ui.actionAddToStartup->setChecked(isAddedToStartup());

    connect(ui.actionAddToStartup, &QAction::triggered, this, [this]() {
        const bool wanted = ui.actionAddToStartup->isChecked();

        if (!addToStartup(wanted)) {
            QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Failed to update the startup entry");
        }

        // Reflect what is actually in the registry rather than what was clicked.
        QSignalBlocker blocker(ui.actionAddToStartup);
        ui.actionAddToStartup->setChecked(isAddedToStartup());
    });

    // ---- File -> Exit ----
    connect(ui.actionExit, &QAction::triggered, this, [this]() { shutdown(0); });

    // ---- Help -> Check for Updates ----
    connect(ui.actionCheckForUpdates, &QAction::triggered, this, [this]() { checkForUpdates(); });

    // ---- Help -> About ----
    connect(ui.actionAbout, &QAction::triggered, this, [this]() {
        about dialog(this);
        dialog.exec();
    });

    // ---- GPU combo box ----
    for (NvU32 i = 0; i < gpuCount; i++) {
        NvAPI_ShortString gpuFullName = {};

        if (NvAPI_GPU_GetFullName(hPhysicalGpus[i], gpuFullName) != 0) {
            return fail("Error: Failed to obtain GPU name");
        }

        ui.selectedGPU->addItem(QString::fromLatin1(gpuFullName));
    }

    // The original compared gpu_index against gpuCount - 1 in both directions,
    // which reduces to "index != gpuCount - 1" and rewrote config.json on every
    // launch on a multi-GPU system.
    int gpuIndex = config["gpu_index"].get<int>();

    if (gpuIndex < 0 || static_cast<NvU32>(gpuIndex) >= gpuCount) {
        gpuIndex = 0;
        config["gpu_index"] = 0;
        persist();
    }

    state.gpuIndex = gpuIndex;
    ui.selectedGPU->setCurrentIndex(gpuIndex);

    connect(ui.selectedGPU, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &limitnvpstate::selectedGPUChanged);

    // ---- P-State combo box ----
    {
        std::vector<int> available;

        if (!queryPStates(gpuIndex, available)) {
            return false;
        }

        const int limit = pickGlobalLimit(available, config["pstate_limit"].get<int>());

        {
            StateGuard guard;
            state.globalLimit = limit;
        }

        showPStates(available, limit);
    }

    connect(ui.selectedPState, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &limitnvpstate::selectedPStateChanged);

    // ---- Unlimit trigger combo box ----
    for (const char* trigger : UNLIMIT_TRIGGERS) {
        ui.unlimitTrigger->addItem(QString::fromLatin1(trigger));
    }

    // Off by one in the original: `> triggerCount` let a stored value of 2 pass,
    // after which setCurrentIndex(2) resolved to -1 and no trigger was installed
    // at all, so the program ran doing nothing.
    int trigger = config["unlimit_trigger"].get<int>();

    if (trigger < 0 || trigger >= UNLIMIT_TRIGGER_COUNT) {
        trigger = TRIGGER_PROCESS_RUNNING;
        config["unlimit_trigger"] = trigger;
        persist();
    }

    state.trigger = static_cast<policy::Trigger>(trigger);
    ui.unlimitTrigger->setCurrentIndex(trigger);

    connect(ui.unlimitTrigger, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &limitnvpstate::unlimitTriggerChanged);

    // ---- Polling interval ----
    {
        int pollMs = config["process_running_polling"].get<int>();

        // Bad configs must neither spin the loop nor park it for weeks.
        if (pollMs < 100) {
            pollMs = 100;
        } else if (pollMs > 600000) {
            pollMs = 600000;
        }

        state.pollMs = static_cast<DWORD>(pollMs);
    }

    // ---- Process exception list ----
    {
        QHeaderView* header = ui.processExceptionsList->header();
        header->setStretchLastSection(false);
        header->setSectionResizeMode(0, QHeaderView::Stretch);
        header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        ui.processExceptionsList->headerItem()->setToolTip(1, QString::fromLatin1(BACKGROUND_TOOLTIP));

        const nlohmann::json& overrides = config["process_background_pstates"];

        std::vector<ProcessEntry> parsed;
        bool                      repaired = false;

        for (const auto& entry : config["process_exceptions"]) {
            if (!entry.is_string()) {
                continue;
            }

            std::wstring name = utf8ToWide(entry.get<std::string>().c_str());

            if (name.empty()) {
                continue;
            }

            // A hand-edited file may list the same exe twice in different case.
            bool duplicate = false;

            for (const ProcessEntry& existing : parsed) {
                duplicate |= sameName(existing.name, name.c_str());
            }

            if (duplicate) {
                repaired = true;
                continue;
            }

            // Case-insensitive, like Windows file names, so a hand-edited key
            // in different case still applies.
            int background = policy::BACKGROUND_DEFAULT;

            for (auto it = overrides.begin(); it != overrides.end(); ++it) {
                if (!sameName(name, utf8ToWide(it.key().c_str()).c_str())) {
                    continue;
                }

                // Range-checked before any conversion: no float or huge
                // integer is ever narrowed into something that looks valid.
                long long  stored = 0;
                const bool isInt  = jsonIntInRange(it.value(), 0, policy::PSTATE_MAX, &stored);

                background = policy::sanitizeBackground(isInt, stored);

                // Default is never written, so a stored key that comes back as
                // Default was garbage: drop it from the file.
                repaired |= background == policy::BACKGROUND_DEFAULT;
                break;
            }

            const int snapped = policy::snapToAvailable(background, availablePStates);
            repaired |= snapped != background;

            addProcessRow(QString::fromStdWString(name), snapped);
            parsed.push_back({ std::move(name), snapped, false });
        }

        publishEntries(std::move(parsed));

        // A sanitised or snapped value: store what is actually in use.
        if (repaired) {
            updateProcessConfig(ui.processExceptionsList);
            persist();
        }
    }

    connect(ui.addProcess,    &QPushButton::clicked, this, &limitnvpstate::addProcess);
    connect(ui.removeProcess, &QPushButton::clicked, this, &limitnvpstate::removeProcess);

    // ---- Initial state: the global limit, until a detector says otherwise ----
    if (!evaluate()) {
        return fail("Error: Failed to set P-State");
    }

    // Logoff/shutdown messages go to top-level windows. Qt creates the native
    // window lazily on first show(), so with Start Minimized there was none and
    // the filter below never saw WM_ENDSESSION. winId() creates it now, hidden.
    (void)winId();

    sessionEndFilter = new SessionEndFilter(this);
    qApp->installNativeEventFilter(sessionEndFilter);

    updateDetectors();

    return true;
}

void limitnvpstate::createTrayIcon() {
    QMenu* trayMenu = new QMenu(this);

    QAction* trayActionExit = new QAction("Exit", this);
    connect(trayActionExit, &QAction::triggered, this, [this]() { shutdown(0); });
    trayMenu->addAction(trayActionExit);

    // PNG, not ICO. Qt decodes PNG inside Qt5Gui; ICO requires the separate
    // imageformats/qico.dll plugin, and dropping it lets the whole imageformats
    // and iconengines directories be deleted from the shipped package.
    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setContextMenu(trayMenu);
    trayIcon->setIcon(QIcon(":/limitnvpstate/icon.png"));
    trayIcon->setToolTip("limit-nvpstate");
    trayIcon->show();

    connect(trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::Trigger) {
            return;
        }

        if (isVisible()) {
            hide();
        } else {
            bringToFront();
        }
    });
}

void limitnvpstate::bringToFront() {
    showNormal();
    raise();
    activateWindow();
}

void limitnvpstate::unlimitTriggerChanged(int index) {
    if (index < 0 || index >= UNLIMIT_TRIGGER_COUNT) {
        return;
    }

    config["unlimit_trigger"] = index;

    {
        StateGuard guard;
        state.trigger = static_cast<policy::Trigger>(index);
    }

    // A poll thread that stays on may be parked without a timeout because a
    // Default row counted as Unlimited under the old trigger. Under the new
    // one it may not, so it must rescan and re-decide how to wait.
    wakePoll();

    // Detectors that stay on keep their state; only what the new trigger
    // does not need is stopped. Then one evaluation from that known state.
    updateDetectors();

    persist();
}

void limitnvpstate::selectedGPUChanged(int index) {
    if (index < 0 || static_cast<NvU32>(index) >= gpuCount) {
        return;
    }

    int previous;

    {
        StateGuard guard;
        previous = state.gpuIndex;
    }

    if (previous == index) {
        return;
    }

    // The P-State list is per GPU. Read it before switching anything, so a
    // failure leaves the old GPU selected and working.
    std::vector<int> available;

    if (!queryPStates(index, available)) {
        QSignalBlocker blocker(ui.selectedGPU);
        ui.selectedGPU->setCurrentIndex(previous);
        return;
    }

    const int limit = pickGlobalLimit(available, config["pstate_limit"].get<int>());

    bool    ok       = true;
    bool    released = true;
    QString failure;

    {
        // One step under one lock: release the old GPU, switch, and move the
        // global limit and every row onto states the new GPU offers. If the
        // lock were dropped between the release and the switch, the poll
        // thread could re-cap the old GPU in the gap, and the switch would
        // then abandon it capped. (Done piecemeal the other way round, it
        // could send the old GPU a P-State only the new one has.)
        StateGuard guard;

        if (state.shuttingDown) {
            return;
        }

        if (state.applied[previous] != APPLIED_NEVER && state.applied[previous] != policy::PSTATE_UNLIMITED) {
            released = applyLocked(previous, policy::PSTATE_UNLIMITED);
        }

        testPause(L"LIMITNV_TEST_PAUSE_GPU_SWITCH");

        if (released) {
            state.gpuIndex    = index;
            state.globalLimit = limit;

            for (ProcessEntry& entry : state.entries) {
                entry.background = policy::snapToAvailable(entry.background, available);
            }

            ok = evaluateLocked();
        } else {
            failure = failureTextLocked();   // before anyone else can overwrite it
        }
    }

    // If the old GPU cannot be released, switching would abandon it capped
    // with nothing left managing it. Stay on it and say so.
    if (!released) {
        {
            QSignalBlocker blocker(ui.selectedGPU);
            ui.selectedGPU->setCurrentIndex(previous);
        }

        QMessageBox::critical(nullptr,
                              "limit-nvpstate",
                              QStringLiteral("Could not release the limit on the current GPU (%1), so it stays "
                                             "selected.").arg(failure));
        return;
    }

    config["gpu_index"] = index;
    persist();

    // The combos and config.json catch up with what is already in effect.
    showPStates(available, limit);

    if (!ok) {
        reportDriverFailure();
    }
}

void limitnvpstate::selectedPStateChanged(int index) {
    // clear() during a GPU switch emits this with -1.
    if (index < 0) {
        return;
    }

    bool      ok     = false;
    const int pstate = ui.selectedPState->itemData(index).toInt(&ok);

    if (!ok) {
        return;
    }

    config["pstate_limit"] = pstate;

    bool applied;

    {
        // Decided and applied under the lock, so a game that started a moment
        // ago can no longer be clamped by a stale "currently limited" check.
        StateGuard guard;
        state.globalLimit = pstate;
        applied           = evaluateLocked();
    }

    if (!applied) {
        reportDriverFailure();
    }

    persist();
}

bool limitnvpstate::queryPStates(int gpuIndex, std::vector<int>& available) {
    available.clear();

    if (gpuIndex < 0 || static_cast<NvU32>(gpuIndex) >= gpuCount) {
        return false;
    }

    // Zero-initialised. The original left every field except `version`
    // indeterminate.
    NV_GPU_PERF_PSTATES20_INFO pStatesInfo = {};
    pStatesInfo.version = NV_GPU_PERF_PSTATES20_INFO_VER;

    if (NvAPI_GPU_GetPstates20(hPhysicalGpus[gpuIndex], &pStatesInfo) != 0) {
        return fail("Error: Failed to obtain available P-States");
    }

    // P0 is the unlimited state, not a valid limit. Skip it by id rather than
    // assuming it is entry 0, and never trust the driver's count past the
    // array the struct actually has.
    const NvU32 count = pStatesInfo.numPstates < NVAPI_MAX_GPU_PSTATE20_PSTATES
                            ? pStatesInfo.numPstates
                            : NVAPI_MAX_GPU_PSTATE20_PSTATES;

    for (NvU32 i = 0; i < count; i++) {
        const int pstate = static_cast<int>(pStatesInfo.pstates[i].pstateId);

        // P1..P15 only: drops P0 and any undefined/sentinel id.
        if (pstate > NVAPI_GPU_PERF_PSTATE_P0 && pstate <= policy::PSTATE_MAX) {
            available.push_back(pstate);
        }
    }

    // Ascending and unique, whatever order the driver used: the combos read
    // naturally, and back() really is the deepest state.
    std::sort(available.begin(), available.end());
    available.erase(std::unique(available.begin(), available.end()), available.end());

    if (available.empty()) {
        return fail("Error: The GPU reported no limitable P-States");
    }

    return true;
}

void limitnvpstate::showPStates(const std::vector<int>& available, int selected) {
    availablePStates = available;
    refreshBackgroundCombos();

    // Rebuild without firing selectedPStateChanged for every intermediate state.
    QSignalBlocker blocker(ui.selectedPState);

    ui.selectedPState->clear();

    for (int pstate : available) {
        ui.selectedPState->addItem(QStringLiteral("P%1").arg(pstate), pstate);
    }

    ui.selectedPState->setCurrentIndex(ui.selectedPState->findData(selected));

    if (config["pstate_limit"].get<int>() != selected) {
        config["pstate_limit"] = selected;
        persist();
    }
}

void limitnvpstate::addProcess() {
    const QStringList selectedFilePaths = QFileDialog::getOpenFileNames(
        this,
        "Select executables",
        QString(),
        tr("Executable Files (*.exe)"));

    if (selectedFilePaths.isEmpty()) {
        return;
    }

    const std::wstring programPath = getProgramPath();
    const QString      ownName     = QString::fromWCharArray(getBaseName(programPath.c_str()));

    bool added = false;

    for (const QString& selectedFilePath : selectedFilePaths) {
        // QFileInfo handles separators, so the manual '/' -> '\\' replacement
        // the original did is unnecessary.
        const QString executableName = QFileInfo(selectedFilePath).fileName();

        if (executableName.isEmpty() || executableName.compare(ownName, Qt::CaseInsensitive) == 0) {
            continue;
        }

        // MatchFixedString is case-insensitive. MatchExactly, which the original
        // used, is not -- so "Game.exe" and "game.exe" both got added despite
        // being the same file on Windows.
        if (!ui.processExceptionsList->findItems(executableName, Qt::MatchFixedString, 0).isEmpty()) {
            continue;
        }

        addProcessRow(executableName, policy::BACKGROUND_DEFAULT);
        added = true;
    }

    if (added) {
        saveProcessExceptions();
    }
}

void limitnvpstate::removeProcess() {
    const QList<QTreeWidgetItem*> selected = ui.processExceptionsList->selectedItems();

    if (selected.isEmpty()) {
        return;
    }

    // Deleting an item removes it from the tree, along with its combo.
    qDeleteAll(selected);

    saveProcessExceptions();
}

void limitnvpstate::addProcessRow(const QString& name, int background) {
    QTreeWidgetItem* item  = new QTreeWidgetItem(ui.processExceptionsList, QStringList(name));
    QComboBox*       combo = new QComboBox();

    combo->setToolTip(QString::fromLatin1(BACKGROUND_TOOLTIP));
    fillBackgroundCombo(combo, background);

    ui.processExceptionsList->setItemWidget(item, 1, combo);

    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) {
            saveProcessExceptions();
        }
    });
}

void limitnvpstate::fillBackgroundCombo(QComboBox* combo, int background) const {
    QSignalBlocker blocker(combo);

    combo->clear();
    combo->addItem(QStringLiteral("Default"),   policy::BACKGROUND_DEFAULT);
    combo->addItem(QStringLiteral("Unlimited"), policy::PSTATE_UNLIMITED);

    for (int pstate : availablePStates) {
        combo->addItem(QStringLiteral("P%1").arg(pstate), pstate);
    }

    const int row = combo->findData(background);
    combo->setCurrentIndex(row >= 0 ? row : 0);
}

void limitnvpstate::refreshBackgroundCombos() {
    bool changed = false;

    for (int i = 0; i < ui.processExceptionsList->topLevelItemCount(); i++) {
        QTreeWidgetItem* item  = ui.processExceptionsList->topLevelItem(i);
        QComboBox*       combo = qobject_cast<QComboBox*>(ui.processExceptionsList->itemWidget(item, 1));

        if (!combo) {
            continue;
        }

        const int current = rowBackground(ui.processExceptionsList, item);
        const int snapped = policy::snapToAvailable(current, availablePStates);

        fillBackgroundCombo(combo, snapped);
        changed |= snapped != current;
    }

    if (changed) {
        saveProcessExceptions();
    }
}

void limitnvpstate::saveProcessExceptions() {
    publishEntries(updateProcessConfig(ui.processExceptionsList));

    // Rescan with the new names now rather than at the next interval, start
    // or stop detectors if a row left or joined Default, and re-evaluate the
    // current foreground against the new list.
    wakePoll();
    updateDetectors();

    // Apply first, save second: a failed save (and its warning) must not hold
    // up the change itself.
    persist();
}

bool limitnvpstate::releaseLimits() {
    // Every GPU we touched, including one the user switched away from. Once
    // released, nothing re-applies a limit: shuttingDown stops evaluation.
    StateGuard guard;
    state.shuttingDown = true;
    return releaseAllLocked().empty();
}

void limitnvpstate::shutdown(int exitCode) {
    beginExit();
    stopAllTriggers();

    // Interactive exit: if the driver refused, say so rather than quitting as
    // if all was well. (Logoff never shows dialogs.)
    if (!releaseLimits()) {
        QMessageBox::warning(nullptr,
                             "limit-nvpstate",
                             QStringLiteral("Could not remove the P-State limit (%1). The GPU may stay limited "
                                            "until it is released, e.g. by running limit-nvpstate again or "
                                            "restarting the PC.").arg(lastFailureText()));
    }

    // Otherwise the icon lingers in the notification area until it is hovered.
    if (trayIcon) {
        trayIcon->hide();
    }

    QCoreApplication::exit(exitCode);
}

void limitnvpstate::setSettingsLocked(bool locked) {
    ui.centralWidget->setEnabled(!locked);
    ui.menuBar->setEnabled(!locked);
}

void limitnvpstate::finishUpdateLater() {
    struct Outcome {
        bool    updated = false;
        QString rolledBack;
    };

    auto               outcome = std::make_shared<Outcome>();
    const std::wstring appDir  = getProgramDirectory();

    updateBusy = true;   // no new update until this one is retired

    runInBackground(this, [appDir, outcome]() {
        using updaterules::Phase;

        // The helper holds the lock until it has decided -- for the new
        // version, a few seconds after it said it is up.
        // Up to 2 minutes: the helper holds the lock while it checks this
        // version (up to 30 s to say it's up, 3 s more, the commit).
        for (int waited = 0; updatetxn::updateLockHeld() && waited < 480; waited++) {
            Sleep(250);
        }

        if (updatetxn::updateLockHeld() || !updatetxn::ownsWorkDir(appDir)) {
            return;
        }

        Phase        phase   = Phase::Swapping;
        bool         present = false;
        std::wstring error;

        if (!updatetxn::readPhase(appDir, phase, present, error)) {
            return;   // reported at startup; nothing is touched
        }

        if (present && phase == Phase::Committed) {
            outcome->updated = true;
            updatetxn::retire(appDir);
        } else if (present && phase == Phase::RolledBack) {
            // Never an undo without a word: the reason may not have been saved.
            const std::wstring result = updatetxn::readResult(appDir);
            outcome->rolledBack       = result.empty() ? QStringLiteral("The update was undone; you are on the previous "
                                                                        "version. The reason could not be saved.")
                                                       : QString::fromStdWString(result);
            updatetxn::retire(appDir);
        } else if (!present && !updatetxn::hasJournal(appDir)) {
            updatetxn::retire(appDir);   // a download that was never installed
        }
        // Anything else is an update the helper still has to finish, or one
        // that needs repair: left exactly as it is.
    }, [this, outcome, appDir]() {
        updateBusy = false;
        setSettingsLocked(false);   // the helper is done with this version

        // Tombstones (this one, and any a crash left) may have to wait out a
        // locked file. They have names no new update uses, so this needn't
        // hold up "Check for Updates".
        runInBackground(this, [appDir]() { updatetxn::deleteTombstones(appDir); }, []() {});

        if (exiting) {
            return;
        }

        if (outcome->updated && trayIcon) {
            trayIcon->showMessage(QStringLiteral("limit-nvpstate"),
                                  QStringLiteral("Updated to version %1.").arg(QString::fromLatin1(APP_VERSION_STRING)),
                                  QSystemTrayIcon::Information,
                                  5000);
        }

        if (!outcome->rolledBack.isEmpty()) {
            showPlain(this, QMessageBox::Warning, outcome->rolledBack);
        }
    });
}

void limitnvpstate::signalReadyLater(HANDLE event) {
    QTimer::singleShot(500, this, [event]() {
        testPause(L"LIMITNV_TEST_PAUSE_BEFORE_READY");

        if (!fatalReported && !exiting && !SetEvent(event)) {
            LOG(L"error: could not tell the updater we are up (%lu)\n", GetLastError());
        }

        CloseHandle(event);
    });
}

void limitnvpstate::checkForUpdates() {
    if (updateBusy || exiting) {
        return;
    }

    updateBusy = true;

    auto             job    = std::make_shared<UpdateJob>();
    QProgressDialog* dialog = makeProgressDialog(this, QStringLiteral("Checking for updates..."), 0);

    connect(dialog, &QProgressDialog::canceled, this, [job]() { job->cancel = true; });

    runInBackground(this, [job]() {
        job->ok = updater::fetchLatest(job->release, job->error);
    }, [this, job, dialog]() {
        dismissProgressDialog(dialog);
        updateBusy = false;

        if (job->cancel || exiting) {
            return;
        }

        const QString current = QString::fromLatin1(APP_VERSION_STRING);
        const QString page    = QString::fromWCharArray(updater::releasesPageUrl());

        if (!job->ok) {
            showPlain(this, QMessageBox::Warning,
                      QStringLiteral("Could not check for updates.\n\n%1\n\nReleases: %2")
                          .arg(QString::fromStdWString(job->error), page));
            return;
        }

        const QString latest = QString::fromStdString(job->release.version);
        const QString notes  = QString::fromStdWString(job->release.pageUrl);

        if (!updaterules::isNewer(job->release.version, APP_VERSION_STRING)) {
            showPlain(this, QMessageBox::Information, QStringLiteral("You have the latest version (%1).").arg(current));
            return;
        }

        if (job->release.checksumUrl.empty()) {
            showPlain(this, QMessageBox::Information,
                      QStringLiteral("Version %1 is available (you have %2).\n\n"
                                     "This release has no checksum file, so it can't be installed "
                                     "automatically. Download it from:\n%3")
                          .arg(latest, current, notes));
            return;
        }

        // The update installs limit-nvpstate.exe; a renamed copy would keep
        // running its old self from the other name.
        if (!runningAsLimitNvpstateExe()) {
            showPlain(this, QMessageBox::Information,
                      QStringLiteral("Version %1 is available (you have %2).\n\n"
                                     "This copy has been renamed, so it can't update itself. Rename it back to "
                                     "limit-nvpstate.exe, or download the new version from:\n%3")
                          .arg(latest, current, notes));
            return;
        }

        // The release page is shown as text, not opened: a browser started
        // from this elevated program would run elevated too.
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("limit-nvpstate"));
        box.setIcon(QMessageBox::Information);
        box.setTextFormat(Qt::PlainText);
        box.setText(QStringLiteral("Version %1 is available (you have %2).\n\n"
                                   "Release notes: %3\n\n"
                                   "Updating restarts limit-nvpstate. Your settings are kept.\n\n"
                                   "Updates are checked for damage but are not signed: install only if you trust "
                                   "this project's GitHub releases.")
                        .arg(latest, current, notes));
        box.setTextInteractionFlags(Qt::TextSelectableByMouse);

        QPushButton* updateNow = box.addButton(QStringLiteral("Update now"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Later"), QMessageBox::RejectRole);
        box.exec();

        if (box.clickedButton() == updateNow && !exiting) {
            startUpdate(job->release);
        }
    });
}

void limitnvpstate::startUpdate(const updater::Release& release) {
    if (updateBusy || exiting) {
        return;
    }

    updateBusy = true;

    auto job     = std::make_shared<UpdateJob>();
    job->release = release;

    QProgressDialog* dialog = makeProgressDialog(
        this, QStringLiteral("Downloading version %1...").arg(QString::fromStdString(release.version)), 100);

    connect(dialog, &QProgressDialog::canceled, this, [job]() { job->cancel = true; });

    QTimer* ticker = new QTimer(dialog);
    connect(ticker, &QTimer::timeout, dialog, [job, dialog]() {
        const long long total    = job->total;
        const long long received = job->received;

        if (total > 0 && received >= 0) {
            dialog->setMaximum(100);
            dialog->setValue(static_cast<int>(received * 100 / total));
        } else {
            dialog->setMaximum(0);   // busy indicator (also while checking/unpacking)
        }
    });
    ticker->start(100);

    const std::wstring appDir = getProgramDirectory();

    runInBackground(this, [job, appDir]() {
        job->ok = updater::prepare(job->release, appDir, job->packageDir, job->error,
                                   [job](long long received, long long total) {
                                       job->received = received;
                                       job->total    = total;
                                       return !job->cancel.load();
                                   });
    }, [this, job, dialog, appDir]() {
        dismissProgressDialog(dialog);

        // Deletes the download off the GUI thread (it may wait out a lock);
        // the updater stays busy until it is gone, so a new download can't
        // start in a folder that is still being deleted.
        const auto discard = [this, appDir]() {
            runInBackground(this, [appDir]() { updater::discardDownload(appDir); }, [this]() { updateBusy = false; });
        };

        // Cancelled -- even after the last step -- or exiting (this can run
        // inside the message loop of an exit warning): install nothing.
        if (job->cancel || exiting) {
            discard();
            return;
        }

        if (!job->ok) {
            discard();
            showPlain(this, QMessageBox::Warning,
                      QStringLiteral("The update was not installed.\n\n%1").arg(QString::fromStdWString(job->error)));
            return;
        }

        // Hand over: the helper swaps the files once this process has exited,
        // when nothing is in use. Stop watching and release the GPU first --
        // the helper never touches it, and the next version starts from a
        // released GPU.
        {
            StateGuard guard;
            state.shuttingDown = true;
        }

        stopAllTriggers();

        if (!releaseLimits()) {
            resumeAfterFailedHandOff(QStringLiteral("Could not remove the P-State limit (%1), so the update was not "
                                                    "installed.").arg(lastFailureText()));
            return;
        }

        // The helper is started, and this copy closes only once the helper
        // has taken over -- waited for in the background (up to 45 s if
        // another update holds the lock). If it hasn't, this copy carries on.
        struct HandOff {
            updater::HandOff result = updater::HandOff::Refused;
            std::wstring     error;
        };

        const auto handOff = std::make_shared<HandOff>();
        const bool visible = isVisible();

        runInBackground(this, [appDir, visible, handOff]() {
            handOff->result = updater::launchHelper(appDir, visible, handOff->error);
        }, [this, handOff]() {
            if (handOff->result != updater::HandOff::Accepted) {
                // Unresolved: the helper may still be there. Its folder is
                // kept and no other update starts in this session.
                resumeAfterFailedHandOff(QStringLiteral("The update was not installed.\n\n%1")
                                             .arg(QString::fromStdWString(handOff->error)),
                                         handOff->result == updater::HandOff::Unresolved);
                return;
            }

            beginExit();

            if (trayIcon) {
                trayIcon->hide();
            }

            QCoreApplication::exit(0);
        });
    });
}

void limitnvpstate::resumeAfterFailedHandOff(const QString& reason, bool helperMayRun) {
    if (!exiting) {
        {
            StateGuard guard;
            state.shuttingDown = false;
        }

        updateDetectors();   // the cache says "released", so the limit is sent again
    }

    // A helper that may still run keeps its folder, and updateBusy stays set:
    // no other update in this session.
    if (!helperMayRun) {
        const std::wstring appDir = getProgramDirectory();
        runInBackground(this, [appDir]() { updater::discardDownload(appDir); }, [this]() { updateBusy = false; });
    }

    showPlain(this, QMessageBox::Warning, reason);
}

void limitnvpstate::closeEvent(QCloseEvent* event) {
    // Hide to the tray. Logoff no longer routes through here: main() turns off
    // Qt's fallback session management, which used to.
    hide();
    event->ignore();
}

void limitnvpstate::changeEvent(QEvent* e) {
    QMainWindow::changeEvent(e);

    if (e->type() == QEvent::WindowStateChange && isMinimized()) {
        hide();
    }
}
