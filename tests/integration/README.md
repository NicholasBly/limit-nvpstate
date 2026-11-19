# limit-nvpstate integration tests (Wine on Linux, and real Windows in CI)

This builds real Windows `.exe`s of any limit-nvpstate source tree and runs
them side by side under Wine, with a fake NVIDIA driver that logs every
P-State limit the app sets. Each scenario starts the app with a given
config.json, plays a timeline (games launching and closing, alt-tabs, UI
changes, logoff), and checks what the GPU is limited to at each step.

It is not a real PC: no NVIDIA GPU, Wine instead of Windows, MinGW + static
Qt 5.15.2 instead of MSVC + Qt 5.14.2. Every variant gets exactly the same
rig, so differences between variants are differences in the app's code.

## Pieces

| File | What it is |
|------|------------|
| `mock/nvapi_mock.cpp` | Fake `nvapi64.dll`. `MOCK_NV_GPUS`, `MOCK_NV_PSTATES<i>`, `MOCK_NV_FAIL_SET_AFTER`, `MOCK_NV_SET_DELAY_MS`, `MOCK_NV_STRICT` |
| `mock/fgshim.cpp`, `include/fgshim.h` | Test-only: emulates `EVENT_SYSTEM_FOREGROUND` (Wine 9 never sends it) and blocks `OpenProcess` query access for `MOCK_BLOCK_QUERY_EXE` (anti-cheat stand-in) |
| `mock/testhook.cpp` | Test-only remote control: drives the real widgets from a command file, and a stand-in for the Add file picker (Qt's froze under Wine) |
| `mock/dummy_game.cpp` | A "game": a window named after its exe. `DUMMY_TITLE`, `DUMMY_NOFG` |
| `mock/fg.cpp` | Alt-tab helper (`fg.exe <title>`, `--query`, `--list`) |
| `tests/utils_test.cpp` | Startup-entry and process-name checks against a variant's `utils.cpp` |
| `variant.pro`, `build_variant.sh`, `build_mocks.sh` | Wine: build one source tree into `out/<name>/`, the stand-ins into `bin/` |
| `build_windows.ps1` | Windows: the same, with MSVC from the real project files |
| `mock/winlist.cpp` | Lists (and closes) plain Windows message boxes |
| `runner.py`, `scenarios*.py` | Scenario runner (Wine or Windows) and scenarios |

## Where it runs

- **Linux + Wine** (this folder's scripts; the before/after rig): every
  scenario, any source tree or commit, MinGW + static Qt 5.15.2.
- **Windows** (CI job `integration` in `.github/workflows/build.yml`, or
  by hand): the update scenarios (`--set=update`) on a real Windows machine,
  built from the real project files with the release's MSVC + Qt 5.14.2 and
  the real `tar.exe`. Scenarios marked `@wine_only` need the Wine rig's
  foreground/process stand-ins (fgshim) or a build only it makes; they are
  skipped there. The same scenario files run on both.

## Setup on Linux (Ubuntu 24.04)

1. `apt-get install mingw-w64 wine64 xvfb nlohmann-json3-dev imagemagick`
2. Cross-build Qt 5.15.2 qtbase for Windows (static) into `$QT5WIN`
   (default `/home/claude/qt5win`; configure line in `qt-configure.txt`;
   add `#include <limits>` to qfloat16.h, qbytearraymatcher.h,
   qoffsetstringarray_p.h for GCC 13).
3. Copy `nlohmann/` (json.hpp) into `include/` (git-ignored).
   `nvwrap/nvapi.h` wraps the repo's NVAPI header with the MSVC SAL macros
   MinGW lacks.
4. `Xvfb :99 &`, `wineboot -i` (prefix: `$WINEPREFIX`, default
   `/home/claude/wineprefix`), and a Wine virtual desktop
   (`HKCU\Software\Wine\Explorer` Desktop=Default, Desktops\Default=1200x780).
5. `./build_mocks.sh`, then copy `bin/fake_tar.exe` to the prefix's
   `drive_c/windows/system32/tar.exe` (Wine has no tar; copy again after
   rebuilding it).
6. Runs go to `$LIMITNV_RUNS` (default: the temp folder).

## Run on Linux

    ./build_variant.sh u3 ../../limit-nvpstate                  # the update tests' names
    APPVER=9.9.9 ./build_variant.sh unew3 ../../limit-nvpstate  # the version in the test package
    python3 runner.py --set=update --variants=u3                 # every update scenario
    python3 runner.py fy3_needs_repair_fails_closed --variants=u3
    ./build_variant.sh orig /path/to/original/limit-nvpstate     # before/after comparisons
    python3 runner.py logoff_window_open --variants=orig,u3

## Run on Windows (by hand)

From an MSVC developer prompt with Qt 5.14.2 (msvc2017_64) on PATH, after
the repo's normal build setup (vcpkg install):

    tests\integration\build_windows.ps1 -Name u3 -Mocks
    tests\integration\build_windows.ps1 -Name unew3 -Version 9.9.9
    python tests\integration\runner.py --set=update --variants=u3

The CI job does exactly this; a manual run of the workflow can name
scenarios to run instead of all of them.

## Wine quirks handled

- No `EVENT_SYSTEM_FOREGROUND`: emulated by fgshim (25 ms foreground poll).
- Qt falls back to Wine's wintab32, whose hidden `WineTabletClass` window
  refuses every logoff. Real Windows 8+ never loads it (Qt uses WM_POINTER),
  so the runner sets `WINEDLLOVERRIDES=wintab32=d`.
- Launching the app hands the foreground to Wine's taskbar (tray icon), so
  "app started while a game is already in front" cannot be set up.

## Updater and second-review scenarios (added 2026-11-18)

- `scenarios_update.py` (upd_*) and `scenarios_review2.py` (r1_..r16_): a
  local HTTP server plays GitHub; builds must define `LIMITNV_UPDATE_TEST`
  and `LIMITNV_TEST_HOOKS` (variant.pro does). The 9.9.9 build that goes in
  the test package: `APPVER=9.9.9 ./build_variant.sh unew2 <src>`.
- Test-only switches (see `limit-nvpstate/testhooks.h`, compiled out of real
  builds): `LIMITNV_TEST_PAUSE_GPU_SWITCH`, `_PAUSE_AFTER_DOWNLOAD`,
  `_PAUSE_INSTALL_STEP`, `_PAUSE_BEFORE_READY` (ms), `_HANDSHAKE_MS`,
  `_FAIL_INSTALL_AT` (step number), `_FAIL_RESTORE` (file or `*`),
  `_FAIL_HASH_READ`.
- Helpers: `mock/fake_tar.cpp` (stored-zip tar.exe for Wine's system32;
  `MOCK_TAR_LINGER_MS` keeps the output folder held open), `mock/filelock.cpp`
  (hold a file open like a virus scanner), `mock/exit3.cpp` (a "new version"
  that exits at once).
- The runner starts Xvfb :99 itself if it is not running; without it Wine
  makes no windows and every foreground test fails for the wrong reason.
- `downgrade_config_in_old_build` is meant for the old PR build (`pr`) only.
