#include <limitnvpstate.h>

#include <about.h>
#include <config.h>
#include <log.h>
#include <nvidia.h>
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
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

// ===========================================================================
// Shared state
// ===========================================================================

namespace {

constexpr int TRIGGER_PROCESS_RUNNING    = 0;
constexpr int TRIGGER_PROCESS_FOREGROUND = 1;

// const char* const, not std::string[]. The original used namespace-scope
// std::string objects here and for NVAPI_DLL, KEY_PATH_RUN, KEY_NAME_RUN and
// CONFIG_MAIN: every one required dynamic initialisation and a heap allocation
// before main() ran, plus static-init registration in the binary.
const char* const UNLIMIT_TRIGGERS[] = { "Process Running", "Process Foreground" };
constexpr int     UNLIMIT_TRIGGER_COUNT = static_cast<int>(std::size(UNLIMIT_TRIGGERS));

NvPhysicalGpuHandle hPhysicalGpus[NVAPI_MAX_PHYSICAL_GPUS] = {};
NvU32               gpuCount = 0;

// Settings the worker thread needs, mirrored out of the nlohmann::json object
// as plain atomics. Reading config["..."] per iteration cost a hash lookup and
// a variant conversion inside the hot loop, and raced with the GUI thread.
std::atomic<int> settingGpuIndex{ 0 };
std::atomic<int> settingPStateLimit{ 8 };
std::atomic<int> settingPollMs{ 5000 };

// Exception list, compared case-insensitively without allocating.
//
// A flat vector beats unordered_set<std::string> at this size: no hashing, no
// per-lookup temporary, contiguous memory, and _wcsicmp bails on the first
// character for the overwhelming majority of comparisons. The strings stay
// wide, so PROCESSENTRY32W::szExeFile is compared in place.
SRWLOCK                   exceptionsLock = SRWLOCK_INIT;
std::vector<std::wstring> exceptions;

// Foreground trigger
HWINEVENTHOOK      eventHook = nullptr;
std::atomic<DWORD> lastForegroundPid{ 0 };

// Process-running trigger
HANDLE pollThread    = nullptr;
HANDLE pollStopEvent = nullptr;   // manual-reset: "shut down"
HANDLE pollWakeEvent = nullptr;   // auto-reset:   "rescan now"

bool isExcepted(const wchar_t* processName) {
    if (!processName || !*processName) {
        return false;
    }

    AcquireSRWLockShared(&exceptionsLock);

    bool found = false;

    for (const std::wstring& exception : exceptions) {
        if (_wcsicmp(exception.c_str(), processName) == 0) {
            found = true;
            break;
        }
    }

    ReleaseSRWLockShared(&exceptionsLock);

    return found;
}

void publishExceptions(std::vector<std::wstring> updated) {
    AcquireSRWLockExclusive(&exceptionsLock);
    exceptions.swap(updated);
    ReleaseSRWLockExclusive(&exceptionsLock);
}

int applyPState(bool isUnlimit) {
    const int gpuIndex = settingGpuIndex.load(std::memory_order_relaxed);

    if (gpuIndex < 0 || static_cast<NvU32>(gpuIndex) >= gpuCount) {
        return 1;
    }

    return setPState(hPhysicalGpus[gpuIndex],
                     isUnlimit,
                     static_cast<unsigned int>(settingPStateLimit.load(std::memory_order_relaxed)));
}

// Constructing a QMessageBox on a non-GUI thread is undefined behaviour. The
// original called QMessageBox::critical directly from the polling thread on two
// error paths. Marshal it onto the GUI thread instead.
void reportFatalFromWorker(const QString& message) {
    QMetaObject::invokeMethod(qApp, [message]() {
        QMessageBox::critical(nullptr, "limit-nvpstate", message);
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);
}

// Evaluates whatever is in the foreground right now. Called when the foreground
// trigger is installed and after the exception list is edited, so a change takes
// effect immediately instead of waiting for the next window switch.
void refreshForegroundState() {
    HWND hwnd = GetForegroundWindow();

    if (!hwnd) {
        return;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    if (pid == 0) {
        return;
    }

    lastForegroundPid.store(pid, std::memory_order_relaxed);

    wchar_t processName[MAX_PATH];

    if (!getProcessNameByPID(pid, processName, MAX_PATH)) {
        return;
    }

    applyPState(isExcepted(processName));
}

// ---------------------------------------------------------------------------
// Foreground trigger
// ---------------------------------------------------------------------------

void CALLBACK winEventProc(HWINEVENTHOOK, DWORD, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    // WINEVENT_OUTOFCONTEXT delivers this on the message loop of the thread that
    // installed the hook, i.e. the GUI thread, so Qt calls below are safe.

    // EVENT_SYSTEM_FOREGROUND also arrives for child objects. Filter to the
    // top-level window transition and use the hwnd the hook gave us; the
    // original overwrote it with GetForegroundWindow(), which races with any
    // further switch that happened before the callback ran.
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) {
        return;
    }

    if (!hwnd) {
        hwnd = GetForegroundWindow();
    }

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
    if (lastForegroundPid.exchange(pid, std::memory_order_relaxed) == pid) {
        return;
    }

    wchar_t processName[MAX_PATH];

    if (!getProcessNameByPID(pid, processName, MAX_PATH)) {
        LOG(L"warn: could not resolve a name for pid %lu\n", pid);
        return;
    }

    const bool excepted = isExcepted(processName);

    LOG(L"info: %s is fg (excepted: %d)\n", processName, static_cast<int>(excepted));

    if (applyPState(excepted) != 0) {
        QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Failed to set P-State");
        QCoreApplication::exit(1);
    }
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

    // Evaluate the current foreground window immediately. The original only
    // applied the limit unconditionally at startup, so launching while a game
    // was already in front left it clamped until the next alt-tab.
    lastForegroundPid.store(0, std::memory_order_relaxed);
    refreshForegroundState();

    return true;
}

void stopForegroundHook() {
    if (!eventHook) {
        return;
    }

    UnhookWinEvent(eventHook);
    eventHook = nullptr;
    lastForegroundPid.store(0, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Process-running trigger
// ---------------------------------------------------------------------------

DWORD WINAPI pollProcesses(LPVOID) {
    // This thread must never compete with a game for CPU.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    for (;;) {
        DWORD matchedPid = 0;

        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

        // CreateToolhelp32Snapshot returns INVALID_HANDLE_VALUE on failure, not
        // NULL. The original tested `if (!hSnapshot)`, which never fires, so a
        // failed snapshot fell through into Process32First on a bad handle.
        if (hSnapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W processEntry;
            processEntry.dwSize = sizeof(processEntry);

            if (Process32FirstW(hSnapshot, &processEntry)) {
                do {
                    // szExeFile is already wide. No conversion, no allocation,
                    // no lowercasing, no hashing, no formatted logging.
                    if (isExcepted(processEntry.szExeFile)) {
                        matchedPid = processEntry.th32ProcessID;
                        break;
                    }
                } while (Process32NextW(hSnapshot, &processEntry));
            }

            CloseHandle(hSnapshot);
        } else {
            LOG(L"error: CreateToolhelp32Snapshot failed (%lu)\n", GetLastError());
        }

        if (applyPState(matchedPid != 0) != 0) {
            reportFatalFromWorker("Error: Failed to set P-State");
            return 1;
        }

        if (matchedPid != 0) {
            HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, matchedPid);

            if (hProcess) {
                LOG(L"info: waiting on pid %lu\n", matchedPid);

                // Zero CPU for as long as the excepted process lives, while
                // still remaining interruptible. The original waited only on the
                // process handle with INFINITE and relied on a bool flag to stop,
                // so the thread could never be shut down or woken.
                const HANDLE waits[3] = { pollStopEvent, pollWakeEvent, hProcess };
                const DWORD  result   = WaitForMultipleObjects(3, waits, FALSE, INFINITE);

                CloseHandle(hProcess);

                if (result == WAIT_OBJECT_0) {
                    return 0;
                }

                // Target exited (or a rescan was requested): scan again straight
                // away. The original slept out a full interval first, holding the
                // GPU at P0 for up to five seconds after the game closed.
                continue;
            }

            LOG(L"warn: could not open pid %lu for synchronisation\n", matchedPid);
        }

        const DWORD  interval = static_cast<DWORD>(settingPollMs.load(std::memory_order_relaxed));
        const HANDLE waits[2] = { pollStopEvent, pollWakeEvent };

        if (WaitForMultipleObjects(2, waits, FALSE, interval) == WAIT_OBJECT_0) {
            return 0;
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

    pollThread = CreateThread(nullptr, 0, pollProcesses, nullptr, 0, nullptr);

    return pollThread != nullptr;
}

void stopPollThread() {
    if (!pollThread) {
        return;
    }

    SetEvent(pollStopEvent);

    // Join. The original detached the thread and only flipped an atomic<bool>,
    // which a thread parked in WaitForSingleObject(..., INFINITE) never observed.
    // Switching triggers back and forth leaked one live thread per switch, all of
    // them still writing P-States.
    if (WaitForSingleObject(pollThread, 5000) == WAIT_TIMEOUT) {
        LOG(L"warn: polling thread did not exit in time\n");
    }

    CloseHandle(pollThread);
    pollThread = nullptr;
}

void wakePoll() {
    if (pollWakeEvent) {
        SetEvent(pollWakeEvent);
    }
}

void stopAllTriggers() {
    stopPollThread();
    stopForegroundHook();
}

// ---------------------------------------------------------------------------
// Session end
// ---------------------------------------------------------------------------

// Without this, a logoff or shutdown terminates the process with the limit still
// applied. A reboot resets the driver anyway, but a logoff does not.
class SessionEndFilter : public QAbstractNativeEventFilter {
public:
    explicit SessionEndFilter(limitnvpstate* owner) : owner(owner) {}

    bool nativeEventFilter(const QByteArray& eventType, void* message, long* result) override {
        Q_UNUSED(eventType);
        Q_UNUSED(result);

        const MSG* msg = static_cast<const MSG*>(message);

        if (msg && (msg->message == WM_QUERYENDSESSION || msg->message == WM_ENDSESSION)) {
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
    ui.setupUi(this);

    // The .ui file declares the window icon with <iconset theme="...">, which
    // generates QIcon::fromTheme() and always returns a null icon on Windows.
    setWindowIcon(QIcon(":/limitnvpstate/icon.png"));
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
            // Reflect what is actually in the registry rather than what was clicked.
            QSignalBlocker blocker(ui.actionAddToStartup);
            ui.actionAddToStartup->setChecked(isAddedToStartup());
        }
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

    settingGpuIndex.store(gpuIndex);
    ui.selectedGPU->setCurrentIndex(gpuIndex);

    // New-style connects: checked at compile time and dispatched without a
    // string lookup, unlike the SIGNAL()/SLOT() macros the original used.
    connect(ui.selectedGPU, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &limitnvpstate::selectedGPUChanged);

    // ---- P-State combo box ----
    if (!getAvailablePStates()) {
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

    ui.unlimitTrigger->setCurrentIndex(trigger);

    connect(ui.unlimitTrigger, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &limitnvpstate::unlimitTriggerChanged);

    // ---- Polling interval ----
    {
        int pollMs = config["process_running_polling"].get<int>();

        if (pollMs < 100) {
            pollMs = 100;   // do not let a bad config spin the poll loop
        }

        settingPollMs.store(pollMs);
    }

    // ---- Process exception list ----
    {
        std::vector<std::wstring> parsed;

        for (const auto& entry : config["process_exceptions"]) {
            if (!entry.is_string()) {
                continue;
            }

            std::wstring name = utf8ToWide(entry.get<std::string>().c_str());

            if (name.empty()) {
                continue;
            }

            ui.processExceptionsList->addItem(QString::fromStdWString(name));
            parsed.push_back(std::move(name));
        }

        publishExceptions(std::move(parsed));
    }

    connect(ui.addProcess,    &QPushButton::clicked, this, &limitnvpstate::addProcess);
    connect(ui.removeProcess, &QPushButton::clicked, this, &limitnvpstate::removeProcess);

    // ---- Initial state ----
    if (setPState(hPhysicalGpus[gpuIndex],
                  false,
                  static_cast<unsigned int>(settingPStateLimit.load()),
                  true) != 0) {
        return fail("Error: Failed to set P-State");
    }

    sessionEndFilter = new SessionEndFilter(this);
    qApp->installNativeEventFilter(sessionEndFilter);

    startTrigger(trigger);

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
            showNormal();
            raise();
            activateWindow();
        }
    });
}

void limitnvpstate::startTrigger(int trigger) {
    if (trigger == TRIGGER_PROCESS_RUNNING) {
        if (!startPollThread()) {
            QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Failed to start the polling thread");
        }
    } else if (trigger == TRIGGER_PROCESS_FOREGROUND) {
        if (!startForegroundHook()) {
            QMessageBox::critical(nullptr, "limit-nvpstate", "Error: Failed to configure the global hook");
        }
    }
}

void limitnvpstate::unlimitTriggerChanged(int index) {
    if (index < 0 || index >= UNLIMIT_TRIGGER_COUNT) {
        return;
    }

    // Tear both down unconditionally. The original stopped whichever trigger the
    // config claimed was active, which drifted out of sync the moment anything
    // failed to start.
    stopAllTriggers();

    config["unlimit_trigger"] = index;
    saveConfig();

    // Start from a known state: the outgoing trigger may have left P0 set.
    applyPState(false);

    startTrigger(index);
}

void limitnvpstate::selectedGPUChanged(int index) {
    if (index < 0 || static_cast<NvU32>(index) >= gpuCount) {
        return;
    }

    const int previousIndex = settingGpuIndex.load();

    // force: the cache says "limited", but that refers to the GPU we are leaving.
    // Without forcing, the outgoing GPU would stay clamped forever.
    if (previousIndex != index && previousIndex >= 0 && static_cast<NvU32>(previousIndex) < gpuCount) {
        setPState(hPhysicalGpus[previousIndex], true, 0, true);
    }

    settingGpuIndex.store(index);
    config["gpu_index"] = index;
    saveConfig();

    // The P-State list is per GPU. The original always queried hPhysicalGpus[0]
    // regardless of which GPU was selected.
    getAvailablePStates();

    setPState(hPhysicalGpus[index],
              false,
              static_cast<unsigned int>(settingPStateLimit.load()),
              true);

    lastForegroundPid.store(0, std::memory_order_relaxed);

    if (eventHook) {
        refreshForegroundState();
    }

    wakePoll();
}

void limitnvpstate::selectedPStateChanged(int index) {
    // clear() during a GPU switch emits this with -1.
    if (index < 0) {
        return;
    }

    bool      ok   = false;
    const int pstate = ui.selectedPState->itemText(index).mid(1).toInt(&ok);   // strip the leading 'P'

    if (!ok) {
        return;
    }

    settingPStateLimit.store(pstate);
    config["pstate_limit"] = pstate;
    saveConfig();

    // Only re-apply if we are currently limited. The original unconditionally
    // forced the new limit, so changing the dropdown while an excepted game was
    // running clamped it mid-session.
    if (!isPStateUnlimited.load()) {
        setPState(hPhysicalGpus[settingGpuIndex.load()],
                  false,
                  static_cast<unsigned int>(pstate),
                  true);
    }
}

bool limitnvpstate::getAvailablePStates() {
    const int gpuIndex = settingGpuIndex.load();

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

    // Rebuild without firing selectedPStateChanged for every intermediate state.
    QSignalBlocker blocker(ui.selectedPState);

    ui.selectedPState->clear();

    const int currentLimit = config["pstate_limit"].get<int>();

    int matchRow = -1;

    // Index 0 is P0, which is the unlimited state and not a valid limit.
    for (NvU32 i = 1; i < pStatesInfo.numPstates; i++) {
        const int pstate = static_cast<int>(pStatesInfo.pstates[i].pstateId);

        ui.selectedPState->addItem(QStringLiteral("P%1").arg(pstate));

        if (pstate == currentLimit) {
            matchRow = ui.selectedPState->count() - 1;
        }
    }

    if (ui.selectedPState->count() == 0) {
        return fail("Error: The GPU reported no limitable P-States");
    }

    if (matchRow < 0) {
        matchRow = ui.selectedPState->count() - 1;   // deepest available state
    }

    ui.selectedPState->setCurrentIndex(matchRow);

    const int selected = ui.selectedPState->itemText(matchRow).mid(1).toInt();

    settingPStateLimit.store(selected);

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
    std::vector<std::wstring> parsed;

    parsed.reserve(static_cast<size_t>(ui.processExceptionsList->count()));

    for (int i = 0; i < ui.processExceptionsList->count(); i++) {
        std::wstring name = ui.processExceptionsList->item(i)->text().toStdWString();

        if (name.empty()) {
            continue;
        }

        list.push_back(wideToUtf8(name.c_str()));
        parsed.push_back(std::move(name));
    }

    config["process_exceptions"] = std::move(list);
    saveConfig();

    publishExceptions(std::move(parsed));

    // Make the edit take effect now rather than at the next window switch or
    // poll interval.
    lastForegroundPid.store(0, std::memory_order_relaxed);

    if (eventHook) {
        refreshForegroundState();
    }

    wakePoll();
}

void limitnvpstate::releaseLimits() {
    // Every GPU, forced: the user may have switched selection during the session.
    for (NvU32 i = 0; i < gpuCount; i++) {
        setPState(hPhysicalGpus[i], true, 0, true);
    }
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
    hide();
    event->ignore();
}

void limitnvpstate::changeEvent(QEvent* e) {
    QMainWindow::changeEvent(e);

    if (e->type() == QEvent::WindowStateChange && isMinimized()) {
        hide();
    }
}
