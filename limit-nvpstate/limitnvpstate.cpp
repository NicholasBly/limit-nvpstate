#include <limitnvpstate.h>

#include <about.h>
#include <config.h>
#include <log.h>
#include <nvidia.h>
#include <pstatepolicy.h>
#include <utils.h>

#include <QAbstractNativeEventFilter>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSystemTrayIcon>

#include <tlhelp32.h>

// Depending on SDK version and WIN32_LEAN_AND_MEAN these may or may not be
// pulled in by winuser.h. Defining them defensively costs nothing.
#ifndef OBJID_WINDOW
#define OBJID_WINDOW ((LONG)0x00000000)
#endif
#ifndef CHILDID_SELF
#define CHILDID_SELF 0
#endif

#include <atomic>
#include <cwchar>
#include <iterator>
#include <string>
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

bool sameName(const std::wstring& a, const wchar_t* b) {
    return _wcsicmp(a.c_str(), b) == 0;
}

// ---- must hold stateLock ---------------------------------------------------

bool applyLocked(int gpu, int target) {
    if (gpu < 0 || static_cast<NvU32>(gpu) >= gpuCount) {
        return false;
    }

    if (state.applied[gpu] == target) {
        return true;
    }

    if (setPStateLimit(hPhysicalGpus[gpu], static_cast<unsigned int>(target)) != 0) {
        state.applied[gpu] = APPLIED_UNKNOWN;
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

void releaseAllLocked() {
    for (NvU32 i = 0; i < gpuCount; i++) {
        if (state.applied[i] != APPLIED_NEVER && state.applied[i] != policy::PSTATE_UNLIMITED) {
            applyLocked(static_cast<int>(i), policy::PSTATE_UNLIMITED);
        }
    }
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
void reportFatal(const char* message) {
    if (fatalReported.exchange(true)) {
        return;
    }

    const QString text = QString::fromLatin1(message);

    QMetaObject::invokeMethod(qApp, [text]() {
        stopAllTriggers();

        {
            StateGuard guard;
            state.shuttingDown = true;
            releaseAllLocked();
        }

        QMessageBox::critical(nullptr, "limit-nvpstate", text);
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);
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
        LOG(L"warn: could not resolve a name for pid %lu\n", pid);
        processName[0] = L'\0';
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
        reportFatal("Error: Failed to set P-State");
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
    // This thread must never compete with a game for CPU.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    std::vector<DWORD>  matchedPids;
    std::vector<HANDLE> waits;

    matchedPids.reserve(MAX_WAIT_PROCESSES);
    waits.reserve(MAXIMUM_WAIT_OBJECTS);

    for (;;) {
        matchedPids.clear();

        bool  ok               = true;
        bool  truncated        = false;
        bool  unlimitedRunning = false;
        DWORD interval         = 5000;

        // CreateToolhelp32Snapshot returns INVALID_HANDLE_VALUE on failure, not
        // NULL.
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

        if (hSnapshot == INVALID_HANDLE_VALUE) {
            LOG(L"error: CreateToolhelp32Snapshot failed (%lu)\n", GetLastError());
        }

        {
            StateGuard guard;

            // A failed snapshot keeps the last known flags rather than
            // pretending everything just exited.
            if (hSnapshot != INVALID_HANDLE_VALUE) {
                for (ProcessEntry& entry : state.entries) {
                    entry.running = false;
                }

                PROCESSENTRY32W processEntry = {};
                processEntry.dwSize          = sizeof(processEntry);

                if (Process32FirstW(hSnapshot, &processEntry)) {
                    do {
                        for (ProcessEntry& entry : state.entries) {
                            if (!sameName(entry.name, processEntry.szExeFile)) {
                                continue;
                            }

                            entry.running = true;

                            if (matchedPids.size() < MAX_WAIT_PROCESSES) {
                                matchedPids.push_back(processEntry.th32ProcessID);
                            } else {
                                truncated = true;
                            }

                            break;
                        }
                    } while (Process32NextW(hSnapshot, &processEntry));
                }
            }

            ok               = evaluateLocked();
            unlimitedRunning = policy::runningContributesUnlimited(state.scratch, state.trigger);
            interval         = state.pollMs;
        }

        if (hSnapshot != INVALID_HANDLE_VALUE) {
            CloseHandle(hSnapshot);
        }

        if (!ok) {
            reportFatal("Error: Failed to set P-State");
            return 1;
        }

        // Wait for any matched process to exit (rescan straight away), a wake
        // request (list edited) or shutdown. If something running already
        // counts as Unlimited, nothing that starts could do better, so there
        // is no need to rescan on a timer: zero CPU for the whole game
        // session. That only holds if we can see every exit, though.
        waits.clear();
        waits.push_back(pollStopEvent);
        waits.push_back(pollWakeEvent);

        bool watchingAll = !truncated;

        for (DWORD pid : matchedPids) {
            HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);

            if (hProcess) {
                waits.push_back(hProcess);
            } else {
                LOG(L"warn: could not open pid %lu for synchronisation\n", pid);
                watchingAll = false;
            }
        }

        const DWORD timeout = (unlimitedRunning && watchingAll && waits.size() > 2) ? INFINITE : interval;
        const DWORD result  = WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, timeout);

        for (size_t i = 2; i < waits.size(); i++) {
            CloseHandle(waits[i]);
        }

        if (result == WAIT_OBJECT_0) {
            return 0;
        }

        if (result == WAIT_FAILED) {
            // Never spin: fall back to a plain interval.
            LOG(L"error: WaitForMultipleObjects failed (%lu)\n", GetLastError());

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

    {
        StateGuard guard;

        trigger = state.trigger;

        for (const ProcessEntry& entry : state.entries) {
            anyOverride |= entry.background != policy::BACKGROUND_DEFAULT;
        }
    }

    const policy::Detectors wanted = policy::detectorsFor(trigger, anyOverride);

    if (!wanted.poll) {
        stopPollThread();
    }

    if (!wanted.hook) {
        stopForegroundHook();
    }

    if (wanted.hook && !startForegroundHook()) {
        QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Failed to configure the global hook");
    }

    if (wanted.poll && !startPollThread()) {
        QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Failed to start the polling thread");
    }

    if (!evaluate()) {
        reportFatal("Error: Failed to set P-State");
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

// ---------------------------------------------------------------------------
// Session end
// ---------------------------------------------------------------------------

// Without this, a logoff terminates the process with the limit still applied.
// WM_ENDSESSION with wParam TRUE means the session really is ending (a
// WM_QUERYENDSESSION alone can still be cancelled by another program).
class SessionEndFilter : public QAbstractNativeEventFilter {
public:
    explicit SessionEndFilter(limitnvpstate* owner) : owner(owner) {}

    bool nativeEventFilter(const QByteArray& eventType, void* message, long* result) override {
        Q_UNUSED(eventType);
        Q_UNUSED(result);

        const MSG* msg = static_cast<const MSG*>(message);

        if (msg && msg->message == WM_ENDSESSION && msg->wParam) {
            stopAllTriggers();
            owner->releaseLimits();
        }

        return false;
    }

private:
    limitnvpstate* owner;
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
        saveConfig();
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
        saveConfig();
    }

    state.gpuIndex = gpuIndex;
    ui.selectedGPU->setCurrentIndex(gpuIndex);

    connect(ui.selectedGPU, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &limitnvpstate::selectedGPUChanged);

    // ---- P-State combo box ----
    if (!getAvailablePStates(gpuIndex)) {
        return false;
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
        saveConfig();
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
        std::vector<ProcessEntry> parsed;

        for (const auto& entry : config["process_exceptions"]) {
            if (!entry.is_string()) {
                continue;
            }

            std::wstring name = utf8ToWide(entry.get<std::string>().c_str());

            if (name.empty()) {
                continue;
            }

            ui.processExceptionsList->addItem(QString::fromStdWString(name));
            parsed.push_back({ std::move(name), policy::BACKGROUND_DEFAULT, false });
        }

        publishEntries(std::move(parsed));
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
    saveConfig();

    {
        StateGuard guard;
        state.trigger = static_cast<policy::Trigger>(index);
    }

    // Detectors that stay on keep their state; only what the new trigger
    // does not need is stopped. Then one evaluation from that known state.
    updateDetectors();
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
    if (!getAvailablePStates(index)) {
        QSignalBlocker blocker(ui.selectedGPU);
        ui.selectedGPU->setCurrentIndex(previous);
        return;
    }

    bool ok;

    {
        StateGuard guard;

        // Release the GPU we are leaving, or it stays clamped forever.
        if (state.applied[previous] != APPLIED_NEVER && state.applied[previous] != policy::PSTATE_UNLIMITED) {
            applyLocked(previous, policy::PSTATE_UNLIMITED);
        }

        state.gpuIndex = index;
        ok             = evaluateLocked();
    }

    config["gpu_index"] = index;
    saveConfig();

    if (!ok) {
        reportFatal("Error: Failed to set P-State");
    }
}

void limitnvpstate::selectedPStateChanged(int index) {
    // clear() during a GPU switch emits this with -1.
    if (index < 0) {
        return;
    }

    bool      ok     = false;
    const int pstate = ui.selectedPState->itemText(index).mid(1).toInt(&ok);   // strip the leading 'P'

    if (!ok) {
        return;
    }

    config["pstate_limit"] = pstate;
    saveConfig();

    bool applied;

    {
        // Decided and applied under the lock, so a game that started a moment
        // ago can no longer be clamped by a stale "currently limited" check.
        StateGuard guard;
        state.globalLimit = pstate;
        applied           = evaluateLocked();
    }

    if (!applied) {
        reportFatal("Error: Failed to set P-State");
    }
}

bool limitnvpstate::getAvailablePStates(int gpuIndex) {
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

    std::vector<int> available;

    // P0 is the unlimited state, not a valid limit. Skip it by id rather than
    // assuming it is entry 0, and never trust the driver's count past the
    // array the struct actually has.
    const NvU32 count = pStatesInfo.numPstates < NVAPI_MAX_GPU_PSTATE20_PSTATES
                            ? pStatesInfo.numPstates
                            : NVAPI_MAX_GPU_PSTATE20_PSTATES;

    for (NvU32 i = 0; i < count; i++) {
        const int pstate = static_cast<int>(pStatesInfo.pstates[i].pstateId);

        if (pstate != NVAPI_GPU_PERF_PSTATE_P0) {
            available.push_back(pstate);
        }
    }

    if (available.empty()) {
        return fail("Error: The GPU reported no limitable P-States");
    }

    // Rebuild without firing selectedPStateChanged for every intermediate state.
    QSignalBlocker blocker(ui.selectedPState);

    ui.selectedPState->clear();

    const int currentLimit = config["pstate_limit"].get<int>();

    int matchRow = -1;

    for (int pstate : available) {
        ui.selectedPState->addItem(QStringLiteral("P%1").arg(pstate));

        if (pstate == currentLimit) {
            matchRow = ui.selectedPState->count() - 1;
        }
    }

    if (matchRow < 0) {
        matchRow = ui.selectedPState->count() - 1;   // deepest available state
    }

    ui.selectedPState->setCurrentIndex(matchRow);

    const int selected = available[static_cast<size_t>(matchRow)];

    {
        StateGuard guard;
        state.globalLimit = selected;
    }

    if (selected != currentLimit) {
        config["pstate_limit"] = selected;
        saveConfig();
    }

    return true;
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
        if (!ui.processExceptionsList->findItems(executableName, Qt::MatchFixedString).isEmpty()) {
            continue;
        }

        ui.processExceptionsList->addItem(executableName);
        added = true;
    }

    if (added) {
        saveProcessExceptions();
    }
}

void limitnvpstate::removeProcess() {
    const QList<QListWidgetItem*> selected = ui.processExceptionsList->selectedItems();

    if (selected.isEmpty()) {
        return;
    }

    for (QListWidgetItem* item : selected) {
        delete ui.processExceptionsList->takeItem(ui.processExceptionsList->row(item));
    }

    saveProcessExceptions();
}

void limitnvpstate::saveProcessExceptions() {
    nlohmann::json            list = nlohmann::json::array();
    std::vector<ProcessEntry> parsed;

    parsed.reserve(static_cast<size_t>(ui.processExceptionsList->count()));

    for (int i = 0; i < ui.processExceptionsList->count(); i++) {
        std::wstring name = ui.processExceptionsList->item(i)->text().toStdWString();

        if (name.empty()) {
            continue;
        }

        list.push_back(wideToUtf8(name.c_str()));
        parsed.push_back({ std::move(name), policy::BACKGROUND_DEFAULT, false });
    }

    config["process_exceptions"] = std::move(list);
    saveConfig();

    publishEntries(std::move(parsed));

    // Rescan with the new names now rather than at the next interval, and
    // re-evaluate the current foreground against the new list.
    wakePoll();
    updateDetectors();
}

void limitnvpstate::releaseLimits() {
    // Every GPU we touched, including one the user switched away from. Once
    // released, nothing re-applies a limit: shuttingDown stops evaluation.
    StateGuard guard;
    state.shuttingDown = true;
    releaseAllLocked();
}

void limitnvpstate::shutdown(int exitCode) {
    stopAllTriggers();
    releaseLimits();

    // Otherwise the icon lingers in the notification area until it is hovered.
    if (trayIcon) {
        trayIcon->hide();
    }

    QCoreApplication::exit(exitCode);
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
