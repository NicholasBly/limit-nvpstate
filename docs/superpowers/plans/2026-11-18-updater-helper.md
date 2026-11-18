# Updater Helper Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the in-app file swap with `limit-nvpstate-updater.exe`, a small helper that swaps files after the app has exited, checks the new version is healthy, and otherwise restores and restarts the old one.

**Architecture:** Pure rules (`updaterules`) gain a zip-directory reader and stricter package/journal/phase rules. A new Win32 transaction module (`updatetxn`) holds the journal, phases, atomic swap, undo and tombstone cleanup, and is compiled into both the app and the helper. The app keeps check/download/verify (`updater`), adds zip pre-validation and launching the helper, and loses install/undo/handshake.

**Tech Stack:** C++17, Win32 (no Qt in the helper, static CRT), Qt 5.14 Widgets app, MSBuild/QtMsBuild, MinGW + Wine rig for tests.

**Spec:** `docs/superpowers/specs/2026-11-18-updater-helper-design.md` (and, for check/download, `2026-11-18-check-for-updates-design.md`).

## Global Constraints

- Helper: no Qt, no NVAPI, static CRT (`/MT`), `requireAdministrator` manifest, file name `limit-nvpstate-updater.exe`.
- Update lock: named mutex `Local\limit-nvpstate-update`, held by the helper for its whole run.
- Helper command line (the only cross-version interface): `install <process handle> [--show]`, `recover`, no arguments = `recover`.
- New app flag: `--after-update <event handle> [--show]`; ready only from the running event loop, ≥ 0.5 s after `init()`, no fatal pending.
- Timeouts: 30 s for the old app to exit, 30 s for "up", 3 s grace; termination wait 10 s.
- Phases (one word in `phase.txt`, replaced atomically): `swapping`, `verifying`, `committed`, `rolling-back`, `rolled-back`, `needs-repair`.
- Journal header `limit-nvpstate update journal 2`; exactly one `P` line first, under `download\unpacked`.
- Package names: printable ASCII; reserved: `limit-nvpstate-update*` (first component), top-level `config.json.bak*`; devices add `CONIN$`, `CONOUT$`.
- Required package files add `limit-nvpstate-updater.exe`.
- Tombstone name: `limit-nvpstate-update.done-<tick>-<pid>`; only folders with `owner.txt` inside are ever deleted.
- Every change gets a before/after Wine test (before = the 3eabc96 build, `u2`, or a copy of it with the same test pause, as with `b1`).

## Review Focus

1. A Windows-startup launch while the helper holds the lock: it must exit, and exactly one instance runs afterwards (test in Task 5: start a normal instance mid-swap).
2. A zip with the expected top folder plus an extra top-level file: refused before unpacking (Task 1 unit test + Task 5 scenario).
3. A tombstone left by a crash during its deletion: deleted at the next start, and nothing without `owner.txt` touched (Task 2/4 scenario: plant a `limit-nvpstate-update.done-x` without marker → kept; with marker → deleted).
4. A journal written by the 3eabc96 dev build (header `journal 1`): treated as `needs-repair`, nothing deleted (Task 1 unit test).
5. The user opening a second copy during the 3 s grace: it signals the running new version to show and exits; the update still commits (Task 5 scenario).

---

### Task 1: Pure rules

**Files:**
- Modify: `limit-nvpstate/updaterules.h`, `limit-nvpstate/updaterules.cpp`
- Test: `tests/updaterules_test.cpp`

**Interfaces — Produces:**
- `struct ZipEntry { std::string name; unsigned long long size = 0; bool directory = false; bool link = false; bool unsupported = false; };`
- `bool findZipDirectory(const std::string& tail, unsigned long long fileSize, unsigned long long& offset, unsigned long long& size, unsigned& count);` — `tail` = last ≤ 65,557 bytes; false on no EOCD, multi-disk, zip64 markers.
- `bool parseZipDirectory(const std::string& directory, unsigned count, std::vector<ZipEntry>& out);` — `unsupported` for encryption or methods other than 0/8; `link` for Unix mode `0120000`; `\` and `/` both separators.
- `std::wstring zipProblem(const std::vector<ZipEntry>& entries, std::wstring& topFolder);` — empty if OK: one top folder holding everything, no top-level files, no links/unsupported, then `packageProblem` on the files under it.
- `bool isReservedPath(const std::wstring& path);`
- `enum class Phase { Swapping, Verifying, Committed, RollingBack, RolledBack, NeedsRepair };` `bool parsePhase(const std::wstring&, Phase&);` `const wchar_t* phaseName(Phase);`
- `parseJournal` / `formatJournal` keep their signatures, header becomes journal 2, and `parseJournal` adds the schema checks.

- [ ] **Step 1: Write failing tests**: `zip_readsDirectoryOfARealArchive` (bytes of a small zip built in the test with stored entries `limit-nvpstate/limit-nvpstate.exe`, `limit-nvpstate/platforms/qwindows.dll`: 2 entries, names, sizes), `zip_backslashNames`, `zip_rejects` (no EOCD, zip64 `0xFFFFFFFF` sizes, encrypted flag, method 12, Unix symlink attrs → `link`), `zipProblem_cases` (extra top-level file, two top folders, `..` entry, duplicate case variant, reserved `limit-nvpstate/limit-nvpstate-update/x`, missing updater exe → non-empty; full package → empty with `topFolder == L"limit-nvpstate"`), `safePath_asciiAndConsoleDevices` (`L"é.dll"`, `CONIN$`, `conout$.txt` false), `reserved_cases`, `phase_roundTrip` (+ `"committed\n"` ok, `"bogus"`/empty false), `journal_v2_schema` (v1 header false; P missing/second/not first/outside `download\unpacked` false; duplicate path (case) false; `R config.json` false; `A config.json` true; reserved target false).
- [ ] **Step 2: Run, expect compile failure**: `g++ -std=c++17 -Wall -Wextra -Ilimit-nvpstate tests/updaterules_test.cpp limit-nvpstate/updaterules.cpp -o /tmp/ur && /tmp/ur`
- [ ] **Step 3: Implement**: little-endian field reads with bounds checks; EOCD search backwards for `PK\5\6`; central entries `PK\1\2` (46-byte header + name + extra + comment).
- [ ] **Step 4: Run with `-fsanitize=address,undefined`**: expected `all tests passed`.
- [ ] **Step 5: Commit** `feat(updaterules): zip directory, reserved names, journal 2, phases`.

### Task 2: Transaction module `updatetxn`

**Files:**
- Create: `limit-nvpstate/updatetxn.h`, `limit-nvpstate/updatetxn.cpp` (moved from `updater.cpp`: file helpers, `listPackage`, `removeTree`, work-folder ownership, journal I/O; new: probes, phases, lock, swap, undo, tombstones)
- Modify: `limit-nvpstate/updater.cpp` (drop what moved), `limit-nvpstate.vcxproj(.filters)`

**Interfaces — Produces (namespace `updatetxn`):**
- `enum class PathState { Missing, File, Directory, Link, Error };` `PathState probe(const std::wstring& path);` (Missing only for `ERROR_FILE_NOT_FOUND`/`ERROR_PATH_NOT_FOUND`)
- `std::wstring workDir(const std::wstring& appDir); std::wstring helperPath(const std::wstring& appDir); std::wstring backupDir(...);`
- `bool ownsWorkDir(const std::wstring& appDir);` `bool openWorkDir(const std::wstring& appDir, std::wstring& error);` (as today; refuses when a journal exists)
- `HANDLE takeUpdateLock(DWORD waitMs);` (nullptr if held; abandoned counts as taken) `bool updateLockHeld();`
- `bool readPhase(const std::wstring& appDir, updaterules::Phase& out, bool& present, std::wstring& error);` `bool writePhase(const std::wstring& appDir, updaterules::Phase, std::wstring& error);` (tmp + flush + `MoveFileEx REPLACE_EXISTING|WRITE_THROUGH`)
- `bool writeResult(appDir, const std::wstring&)`, `std::wstring readResult(appDir)`
- `bool findPackage(const std::wstring& appDir, std::wstring& packageDir, std::wstring& error);`
- `enum class SwapResult { Swapped, RolledBack, NeedsRepair };` `SwapResult swapIn(const std::wstring& packageDir, const std::wstring& appDir, std::wstring& error);` — plan (exe last), journal (CREATE_NEW, flushed), phase `swapping`, per file: `CreateHardLinkW(backup, target)` else `CopyFileW`, then `MoveFileExW(source, target, REPLACE_EXISTING|WRITE_THROUGH)`; failure → `undo`.
- `bool undo(const std::wstring& appDir, std::wstring& error);` — phase `rolling-back` first; reverse journal; `R`: backup present → rename over target; `A`: source gone → delete target (in-use → unique `trash\<n>`); `D`: remove if empty; any `Error` probe → false (caller sets `needs-repair`).
- `bool retire(const std::wstring& appDir);` (rename work folder → tombstone, then delete it) `void deleteTombstones(const std::wstring& appDir);` (only those with `owner.txt`)
- `bool removeTree(const std::wstring& dir);` (unchanged)
- Test switches: `LIMITNV_TEST_PAUSE_SWAP_STEP` (ms, before each file), `LIMITNV_TEST_FAIL_SWAP_AT` (step), `LIMITNV_TEST_FAIL_RESTORE` (file or `*`).

- [ ] **Step 1:** Move the existing file helpers/`listPackage`/`removeTree`/ownership/journal I/O out of `updater.cpp`; app still builds (`./build_variant.sh u3 <src>`), the existing upd_* scenarios pass on `u3`.
- [ ] **Step 2:** Add probes, phases, lock, `swapIn`, `undo`, `retire`, `deleteTombstones` as above. (Exercised through the helper in Task 3; no separate harness.)
- [ ] **Step 3: Commit** `refactor(updater): transaction module shared by app and helper`.

### Task 3: The helper program

**Files:**
- Create: `updater-helper/main.cpp`, `updater-helper/limit-nvpstate-updater.vcxproj` (+ `.filters`), `updater-helper/app.manifest`
- Modify: `limit-nvpstate.sln` (second project, x64 Debug/Release), `build.ps1` (copy `x64\Release\limit-nvpstate-updater.exe` to the stage folder; fail if missing), `limit-nvpstate/updaterules.cpp` (required file), `/home/claude/harness/build_variant.sh` (MinGW build of the helper into `out/<name>/`)

**Interfaces — Consumes:** Task 1 and 2. **Produces:** `limit-nvpstate-updater.exe` with the command line from Global Constraints; exit codes `0` done/committed/rolled back, `1` refused (lock held, bad arguments), `2` needs repair.

- [ ] **Step 1:** `appDir` = exe folder, or two up when the exe sits in `limit-nvpstate-update\helper\`; must contain `limit-nvpstate.exe`.
- [ ] **Step 2:** `install`: lock (30 s) → wait on the inherited handle (`WAIT_OBJECT_0` within 30 s, else result "the previous version did not close", release lock, start old app, exit) → `findPackage` → `swapIn` → phase `verifying` → start new exe with an inheritable event (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`, only that handle) → wait event/child 30 s (`LIMITNV_TEST_HANDSHAKE_MS`) → event and child alive after 3 s → `committed`, release lock, exit 0.
- [ ] **Step 3:** Failure path: terminate if alive; anything but `WAIT_OBJECT_0` within 10 s (`LIMITNV_TEST_FAIL_TERMINATE` simulates) → result "restart Windows to finish", leave phase, release lock, exit 2 without starting anything. Else `undo` → `rolled-back` + result reason, or `needs-repair`; release lock; start old app (`--show` passed on); exit.
- [ ] **Step 4:** `recover`: lock; phase `committed`/`rolled-back`/`needs-repair` or no journal → just start the app; otherwise `undo` → `rolled-back` + "an update was interrupted, so it was undone" (or `needs-repair`); release lock; start the app.
- [ ] **Step 5:** MSVC project: `/MT`, `/W4`, `/sdl` in Release, manifest `requireAdministrator`, sources `main.cpp`, `..\limit-nvpstate\updatetxn.cpp`, `..\limit-nvpstate\updaterules.cpp`, links `Shlwapi.lib` only if needed. Rig: `x86_64-w64-mingw32-g++ -municode -static -DLIMITNV_TEST_HOOKS`.
- [ ] **Step 6: Commit** `feat: limit-nvpstate-updater.exe helper`.

### Task 4: App side

**Files:**
- Modify: `limit-nvpstate/updater.{h,cpp}`, `limit-nvpstate/limitnvpstate.{h,cpp}`, `limit-nvpstate/main.cpp`
- Test (rig): `/home/claude/harness/mock/fgshim.cpp` (`MOCK_FAIL_HOOK_FILE=p`: while file p exists, `SetWinEventHook` fails), `mock/ready_then_exit.cpp` (sets the `--after-update` event, exits 0)

**Interfaces — Consumes:** Task 1–3. **Produces:**
- `updater::prepare(...)` unchanged signature; adds: read zip directory from the file (`findZipDirectory`/`parseZipDirectory`/`zipProblem`) before `tar`, unpacked tree must equal the zip's file list, copy `<package>\limit-nvpstate-updater.exe` to `updatetxn::helperPath(appDir)`.
- `bool updater::launchHelper(const std::wstring& appDir, bool show, std::wstring& error);` (inherits a `SYNCHRONIZE` duplicate of the current process, handle list only)
- `enum class updater::StartupState { Normal, Recovering, NeedsRepair, Finished };` `StartupState updater::checkAtStartup(const std::wstring& appDir, std::wstring& message);` — `Recovering` means: helper started in `recover`, exit now.
- `void limitnvpstate::finishUpdateLater(bool afterUpdate);` (background: wait until `!updatetxn::updateLockHeld()`, read phase/result, queue the notice, `retire`)
- `void limitnvpstate::signalReadyLater(HANDLE event);` (500 ms single-shot inside `exec()`; skip if `fatalReported || exiting`; `SetEvent` result checked)
- Removed: `updater::install`, `undoInstall`, `markConfirmed`, `finishUpdate`, `recoverAtStartup`, `limitnvpstate::restartIntoUpdate`, `abandonUpdate`, `yieldGpu`.

- [ ] **Step 1:** `main.cpp`: parse `--after-update <event>`; if not after-update and `updateLockHeld()` → exit 0; single instance; if not after-update → `checkAtStartup` (`Recovering` → exit 0 before `initConfig`); after `init()` → `signalReadyLater` (after-update) and `finishUpdateLater`; `deleteTombstones` in the background.
- [ ] **Step 2:** `startUpdate` completion: cancel/exiting/!ok → discard (as today); else stop watching, release GPU (failure → resume + message), `launchHelper` (failure → resume + message), hide tray, `QCoreApplication::exit(0)`.
- [ ] **Step 3:** Delete the removed functions; `updater.h` comment block describes the helper flow.
- [ ] **Step 4: Commit** `feat(updater): hand the install to the helper`.

### Task 5: Tests, regression, CI

**Files:**
- Modify: `/home/claude/harness/scenarios_update.py` (package includes `out/<new>/limit-nvpstate-updater.exe`; `NEW_FOR['u3'] = 'unew3'`), create `/home/claude/harness/scenarios_review3.py`, `runner.py` import.
- Before builds: `u2` (3eabc96) and `b2` = 3eabc96 + `testPause(L"LIMITNV_TEST_PAUSE_BETWEEN_MOVES")` between the two renames of each replaced file.

- [ ] **Step 1: Scenarios** (each: fails on the before build for the stated reason, passes on `u3`):
  - `t1_kill_between_moves_cold_start` (b2: kill between the exe's two renames → cold start has no exe; u3: kill the helper during each of steps 1, middle, last → cold start launches recovery, every file equals the pre-update snapshot, app running with "interrupted" message).
  - `t2_ready_then_exit` (new exe = `ready_then_exit.exe` → u2 commits; u3 undoes and restarts the old version).
  - `t2_new_version_hook_fails` (`MOCK_FAIL_HOOK_FILE` created after the old app started → new version queues a fatal → u3 never signals ready → rollback).
  - `t3_killed_child_left_a_limit` (old wants P0 with gameA running; new = hang build that applies P8 then hangs → after rollback the driver log's last write is P0 from the restarted old version and gameA is still tracked).
  - `t4_crash_during_cleanup` (pause in `retire` before rename / kill after → next start: nothing rolled back; a committed update stays).
  - `t5_update_while_cleanup_retries` (lock a file inside the old work folder so retire retries; start a new update → new download survives).
  - `t7_child_wont_die` (`LIMITNV_TEST_FAIL_TERMINATE` → nothing restored, no old app started, message).
  - `t8_zip_rejected_before_unpack` (extra top-level file; duplicate case entry; symlink entry; oversized declared size → refused, unpack folder never created).
  - `t9_reserved_paths` (package with `limit-nvpstate-update/x` and `config.json.bak` → refused).
  - `t11_retry_undo_after_crash` (crash during undo, run recovery twice → backups intact, files restored).
  - Review Focus 1, 3, 5 scenarios; existing upd_* and r* updated for the new flow.
- [ ] **Step 2:** Run new scenarios on before builds and `u3`; record failure reasons.
- [ ] **Step 3:** Full regression of every scenario on `u3` (expect all pass except `downgrade_config_in_old_build`, which passes on `pr`).
- [ ] **Step 4:** Push the feature branch (not `main`); verify CI builds both projects, runs both unit suites, and the artifact contains `limit-nvpstate-updater.exe`.
- [ ] **Step 5:** Update the check-for-updates design doc's install section to point at the helper design; commit.
