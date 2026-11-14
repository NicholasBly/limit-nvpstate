# Per-exe "When behind" P-State Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the PR #11 review findings, then let each exception-list exe carry a "When behind" P-State (Fortnite: P0 in front, P3 behind, global P8 when closed).

**Architecture:** All limit decisions move into one pure function (`policy::computeTarget`) fed from one shared state object guarded by one lock, so the poll thread and the GUI thread can never apply limits out of order. Detectors (process poll, foreground hook) become inputs that are started/stopped by a single `updateDetectors()` reconciler.

**Tech Stack:** C++17, MSVC v143, Qt 5.14.2 Widgets, NVAPI, nlohmann-json (vcpkg). Verification here: native g++ for the pure module, `x86_64-w64-mingw32-g++ -fsyntax-only` against Linux Qt5 headers + repo NVAPI headers for the Windows files.

**Spec:** `docs/superpowers/specs/2026-11-13-per-exe-background-pstate-design.md`

## Global Constraints

- Config key: `process_background_pstates`, object `{ "exe name": pstate }`, pstate 0..15; missing = Default.
- Old builds must still read the config (no change to `process_exceptions` shape: array of strings).
- In front always = P0. Lowest P number wins. Nothing counts = global `pstate_limit`.
- Default row: Running trigger = Unlimited when running; Foreground trigger = counts as nothing when behind.
- Any non-Default row ⇒ both detectors run.
- PR fixes land as commits before the feature commit (cherry-pickable into PR #11).
- No new runtime dependencies; no `<iostream>`/`<fstream>`.

## Review Focus

1. Hand-edited config with a bad override value (string, 99, -3) → treated as Default, no crash. Test: `sanitizeBackground_rejectsBadValues` (Task 2).
2. Override slower than the global limit (P12 vs P8) → override still applies while behind. Test: `override_slowerThanGlobal_stillApplies` (Task 2).
3. GPU switch where the saved override doesn't exist → snapped to nearest available, ties to the lower-power state. Test: `snap_*` (Task 2).
4. Exe name case mismatch between list, override map and running process → still matches (`_wcsicmp` everywhere; case-insensitive map lookup at load). Manual check in Task 4.
5. Same exe with several processes, one exits → state unchanged; the poll waits on every matched handle and rescans on any exit (Task 3).

---

### Task 1: Standalone PR #11 fixes

**Files:**
- Modify: `limit-nvpstate/utils.cpp` (startup key, name fallback, log), `limit-nvpstate/main.cpp` (second instance, app icon), `limit-nvpstate/limitnvpstate.cpp` (`getAvailablePStates`, poll clamp)

**Interfaces:**
- Produces: `isAddedToStartup()` true if either Run view has the value; `addToStartup(true)` writes 64-bit view and deletes the 32-bit (`KEY_WOW64_32KEY`) one; `addToStartup(false)` deletes both.
- Produces: named auto-reset event `Local\limit-nvpstate-show`; second instance calls `AllowSetForegroundWindow(ASFW_ANY)` + `SetEvent`; first instance watches it with `QWinEventNotifier` and does `showNormal(); raise(); activateWindow();`. Remove `FindWindowW`.

- [ ] **Step 1:** `getProcessNameByPID`: on `OpenProcess`/query failure, fall back to a `TH32CS_SNAPPROCESS` lookup by pid (restores the original anti-cheat path).
- [ ] **Step 2:** `openRunKey` logs the returned `LSTATUS`, not `GetLastError()`.
- [ ] **Step 3:** `getAvailablePStates`: loop `min(numPstates, NVAPI_MAX_GPU_PSTATE20_PSTATES)`, skip by `pstateId == NVAPI_GPU_PERF_PSTATE_P0`, not index 0.
- [ ] **Step 4:** `process_running_polling` clamped to `[100, 600000]`.
- [ ] **Step 5:** `QApplication::setWindowIcon(QIcon(":/limitnvpstate/icon.png"))` in `main` (About dialog + message boxes).
- [ ] **Step 6:** Verify: mingw syntax check of `utils.cpp` and `main.cpp` passes with no errors.
- [ ] **Step 7:** Commit `fix: startup key migration, protected-process names, second-instance activation`.

### Task 2: Pure policy module

**Files:**
- Create: `limit-nvpstate/pstatepolicy.h`, `limit-nvpstate/pstatepolicy.cpp`, `tests/pstatepolicy_test.cpp`
- Modify: `limit-nvpstate/limit-nvpstate.vcxproj`, `.vcxproj.filters` (add the two files)

**Interfaces (namespace `policy`, no Windows/Qt includes):**
- `constexpr int BACKGROUND_DEFAULT = -1; constexpr int PSTATE_UNLIMITED = 0; constexpr int PSTATE_MAX = 15;`
- `enum class Trigger { ProcessRunning = 0, ProcessForeground = 1 };`
- `struct EntryState { int background; bool running; bool foreground; };` (caller masks `running`/`foreground` to false when that detector is off)
- `int computeTarget(const std::vector<EntryState>&, Trigger, int globalLimit);`
- `bool runningContributesUnlimited(const std::vector<EntryState>&, Trigger);` (ignores `foreground`)
- `struct Detectors { bool poll; bool hook; }; Detectors detectorsFor(Trigger, bool anyOverride);`
- `int snapToAvailable(int pstate, const std::vector<int>& available);` (passes through `BACKGROUND_DEFAULT`, `0`, and empty `available`; tie → higher number)
- `int sanitizeBackground(bool isInteger, long long value);` (`BACKGROUND_DEFAULT` unless integer in 0..15)

- [ ] **Step 1: Write the failing tests** in `tests/pstatepolicy_test.cpp` (plain `assert`-style `CHECK` macro, `main` returns failures count). Cases, global limit 8:
  - `default_foregroundTrigger`: fg → 0; running behind → 8; closed → 8
  - `default_runningTrigger`: running → 0; closed → 8
  - `override3_bothTriggers`: fg → 0; behind → 3; closed → 8
  - `overrideUnlimited_behind`: background 0 behind, Foreground trigger → 0
  - `override_slowerThanGlobal_stillApplies`: background 12 behind → 12
  - `mix`: {3 behind, 5 behind} → 3; {3 behind, default behind, Foreground} → 3; {3 behind, other fg} → 0; empty → 8
  - `runningContributesUnlimited`: Running+default running → true; Foreground+default running → false; override 0 running → true; override 3 running → false; foreground-only entry → false
  - `detectorsFor`: (Running,false)={1,0}; (Foreground,false)={0,1}; (either,true)={1,1}
  - `snap_*` with {2,3,5,8}: 3→3, 4→5, 6→5, 1→2, 15→8, 0→0, -1→-1; empty → 4→4
  - `sanitizeBackground_rejectsBadValues`: (true,3)→3, (true,0)→0, (true,15)→15, (true,16)→-1, (true,-3)→-1, (false,3)→-1
- [ ] **Step 2:** Run `g++ -std=c++17 -Wall -Wextra -Ilimit-nvpstate tests/pstatepolicy_test.cpp limit-nvpstate/pstatepolicy.cpp -o /tmp/policy_test && /tmp/policy_test` → link error (not implemented).
- [ ] **Step 3:** Implement the five functions.
- [ ] **Step 4:** Same command → `all tests passed`, exit 0.
- [ ] **Step 5:** Commit `feat: pure P-State policy module with tests`.

### Task 3: One shared state, one evaluator (fixes the PR races)

**Files:**
- Modify: `limit-nvpstate/nvidia.h`, `limit-nvpstate/nvidia.cpp`, `limit-nvpstate/limitnvpstate.cpp`, `limit-nvpstate/limitnvpstate.h`

**Interfaces:**
- Consumes: everything in Task 2.
- Produces (nvidia): `int setPStateLimit(NvPhysicalGpuHandle, unsigned int limit);` raw call, 0 = unlimited. Remove `isPStateUnlimited` and `setPState`.
- Produces (limitnvpstate.cpp anon namespace): `struct ProcessEntry { std::wstring name; int background; bool running; };` and one `SharedState` (entries, foreground exe name, `pollActive`, `hookActive`, trigger, gpuIndex, globalLimit, `applied[NVAPI_MAX_PHYSICAL_GPUS]` init -1, `shuttingDown`) behind `SRWLOCK stateLock`. `bool evaluateLocked();` `bool evaluate();` `void reportFatal(const QString&)` (once-only, queued to GUI: stop detectors, release, box, `exit(1)`).
- Produces (class): `void updateDetectors();` `void publishEntries(std::vector<ProcessEntry>)` (carries `running` over by name).

- [ ] **Step 1:** Poll thread: holds `stateLock` while walking the snapshot (marks `running`, collects ≤62 pids), calls `evaluateLocked`, reads `runningContributesUnlimited`; outside the lock opens `SYNCHRONIZE` handles and waits on {stop, wake, handles…} with `INFINITE` only if unlimited-running and every handle opened, else the poll interval.
- [ ] **Step 2:** Hook: same-pid short-circuit kept; sets `foreground` name under the lock and `evaluateLocked`; if the name is a listed exe and the poll is active, `wakePoll()`.
- [ ] **Step 3:** GPU change releases the old GPU and switches index under the lock, then `getAvailablePStates()` and `evaluate()`. P-State change: set `globalLimit` under the lock, `evaluate()`. Trigger change: set trigger, `updateDetectors()`. Never stop/join the poll thread while holding `stateLock`; join with `INFINITE`.
- [ ] **Step 4:** Session end: `winId()` in `init()`; filter acts on `WM_ENDSESSION` with `wParam == TRUE` only: stop detectors, `releaseLimits()`. `releaseLimits()` sets `shuttingDown` and unlimits only GPUs whose `applied > 0`.
- [ ] **Step 5:** `init()`: `evaluate()` once (fail → `fail("Error: Failed to set P-State")`), then `updateDetectors()`.
- [ ] **Step 6:** Verify: mingw syntax check of `nvidia.cpp`, `limitnvpstate.cpp` (with `uic`-generated header) passes; Task 2 tests still pass.
- [ ] **Step 7:** Commit `fix: serialize all P-State decisions behind one lock and per-GPU cache`.

### Task 4: "When behind" config + UI

**Files:**
- Modify: `limit-nvpstate/limitnvpstate.ui`, `limitnvpstate.cpp/.h`, `config.cpp` (default + `sameKind` objects), `config.json`

**Interfaces:**
- Consumes: Tasks 2–3.
- Produces: `processExceptionsList` becomes a 2-column `QTreeWidget` ("Process", "When behind"), per-row `QComboBox` via `setItemWidget`, items `Default(-1)`, `Unlimited(0)`, then the GPU's states; window 600×270 (min 600×240), group max width 400; `ExtendedSelection`.

- [ ] **Step 1:** `config.cpp`: default `process_background_pstates: {}`; `sameKind` accepts two objects (otherwise every launch wipes it).
- [ ] **Step 2:** Load: case-insensitive lookup of each exception name in the object, `sanitizeBackground`, then `snapToAvailable`.
- [ ] **Step 3:** Save: names array unchanged; object holds only non-Default rows; `publishEntries`, `wakePoll`, `updateDetectors`.
- [ ] **Step 4:** GPU change rebuilds every row's combo with snapping and saves if anything moved.
- [ ] **Step 5:** Header tooltip on "When behind": `P-State to use while this program is open but not in front.\nDefault: Running trigger = Unlimited, Foreground trigger = the Limit P-State.`
- [ ] **Step 6:** Verify: `uic` regenerates cleanly; mingw syntax check passes; manual read-through of name matching (Review Focus 4).
- [ ] **Step 7:** Commit `feat: per-exe "When behind" P-State`.

### Task 5: Docs and final review

- [ ] **Step 1:** README: one paragraph on "When behind", the Fortnite exe name (`FortniteClient-Win64-Shipping.exe`), and the exclusive-fullscreen caveat.
- [ ] **Step 2:** Fresh reviewer agent reads the whole branch diff against spec + plan.
- [ ] **Step 3:** Fix findings, push `feature/per-exe-background-pstate`.
