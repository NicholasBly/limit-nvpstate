# Updater helper: design

Supersedes the "Update now" install steps of
`2026-11-18-check-for-updates-design.md` (checking, downloading and verifying
stay as they are).

## Why

The third review (3eabc96) showed the remaining updater risks share one root
cause: the running app replaces its own files. Windows won't overwrite a file
that is in use (the exe, its Qt DLLs), so every swap is two moves with a
moment where the file is missing; a crash there can leave an app that can't
start to repair itself (T1). The rest follow from the old and new instance
overlapping: readiness signalled too early (T2), a killed new instance's GPU
limit (T3), cleanup racing (T4, T5), recovery continuing in the wrong code
(T6), unchecked process waits (T7), shared config/mutex during the handover
(T12).

## Goals

- A crash or power cut at any moment of an update leaves an app that starts
  and recovers, or at worst a recovery program that does (no Qt, no NVAPI).
- At most one process manages the GPU at any time.
- A new version counts as installed only after it has run healthily for a
  moment; otherwise the previous version is back, running.
- Cleanup can never undo a finished update or delete someone else's files.

## Non-goals

- Release signing (T10): still the documented same-release checksum.
- Side-by-side version folders or a permanent launcher: the install layout and
  the startup entry stay as they are.
- Changing how the limiter detects games (carry-over items).

## Approaches considered

- **Patch in place:** fix each T item inside the running app. Cheapest, but
  the missing-file gap (T1) can't be closed while the files are in use.
- **Separate helper (chosen by the user):** a tiny program does the swap after
  the app has exited. Nothing is in use, so each file is replaced in one
  atomic rename.
- **Check-only:** no install at all; safe, but not what was asked.

## The helper

`limit-nvpstate-updater.exe`: plain Win32 C++, static CRT, no Qt, no NVAPI,
`requireAdministrator` manifest (so running it by hand asks for admin, and
started by the elevated app it asks nothing). A second project in the
solution; `build.ps1` puts it next to the exe, so it ships in every release
zip and is a required package file.

It never touches the GPU. It holds a lock, `Local\limit-nvpstate-update`,
for as long as it works; a normal start of the app that finds the lock taken
just exits (an update is running; the helper will start the app).

## Flow

1. **App (old version), as today:** check, download, verify the checksum.
   **New:** before unpacking, read the zip's own file list and refuse it
   unless every entry is a plain file/folder with a safe ASCII name, under one
   top folder, no duplicates (case-insensitive), no links, within the count
   and size limits (T8). Unpack, check the unpacked tree matches that list.
2. **App:** copy the package's helper to `limit-nvpstate-update\helper\`
   (inside the work folder, never part of the swap; failure: abort, keep
   running). Stop watching, release the GPU (failure: abort, keep running).
   Start the helper with an inherited handle to the app's own process (and
   `--show` if the window is open), then exit.
3. **Helper:** take the update lock. Wait for the app's process handle to be
   signalled (anything but `WAIT_OBJECT_0` within 30 s: abort without
   changes, start the old app again). Plan the install (as today, plus the
   rules in "Package and journal rules"), write `journal.txt`, set the phase
   to `swapping`.
4. **Helper, each file:** back up the current one (hard link into
   `backup\`, or a copy where hard links aren't supported), then rename the
   new file over it (`MoveFileEx REPLACE_EXISTING | WRITE_THROUGH`). The
   target always exists: old, then new.
5. **Helper:** phase `verifying`. Start the new exe with
   `--after-update <inherited event handle> [--show]`. The new app starts
   normally (it takes the single-instance mutex itself, there is no other
   instance; the flag lets it start while the update lock is held), and sets
   the event only from inside its running event loop, at least 0.5 s after
   `init()` returned, and only if no fatal error is pending (T2).
6. **Helper:** success = the event is set within 30 s **and** the new
   process is still running 3 s later. Then phase `committed`, release the
   lock, exit.
   Failure (exited, timed out, or died in the grace period): stop it and
   require `WAIT_OBJECT_0` (T7) — if it can't be confirmed gone, leave
   everything as is, phase stays, record "restart Windows to finish", exit.
   Otherwise phase `rolling-back`, undo, phase `rolled-back`, record the
   reason in `result.txt`, start the old exe (normal start, `--show` passed
   on), exit.
7. **App (new or restored), once running:** in the background, wait for the
   update lock to be free (the helper has exited), then read the work folder:
   `committed` → show "Updated to X"; `rolled-back` → show `result.txt`
   ("The update to X didn't work: ... You're still on Y"). Then retire it:
   rename the whole folder to a unique tombstone
   (`limit-nvpstate-update.done-<id>`) and delete that in the background. A
   tombstone is never read again, only deleted (T4, T5).

GPU (T3): the old app released before exiting; the helper never writes; a
failed new instance is confirmed dead before the old one is started, and a
fresh start always sends its first decision to the driver (its cache starts
"never applied"), so a limit the dead instance left behind is overwritten.

## Recovery after a crash

A normal start that finds the lock free and the work folder with a phase of
`swapping`, `verifying` or `rolling-back` (the helper died) does not start
the limiter: it starts the helper in `recover` mode and exits (T6). The
helper undoes, sets `rolled-back`, records "an update was interrupted",
starts the app. Phase file unreadable, or journal invalid → "needs repair":
nothing is changed or deleted, the app says where the backups are, and
further updates are refused until it is resolved.

If the app itself can't start, running `limit-nvpstate-updater.exe` (next to
the exe, or the copy in `limit-nvpstate-update\helper\`) with no arguments
does the same recovery. Every file is always present (step 4), and Qt is the
same version in every release, so this should not be needed.

## Package and journal rules (T9, T11)

- Package names: ASCII printable only; no updater namespace
  (`limit-nvpstate-update*`), no `config.json.bak*`; device names also
  include `CONIN$`, `CONOUT$` and `COM`/`LPT` with superscript digits.
- Required files: as today plus `limit-nvpstate-updater.exe`.
- Journal: header "journal 2", exactly one `P` line first, under
  `download\unpacked`; every path unique (case-insensitive) and not reserved;
  `config.json` never an `R`.
- Phase: one word in `phase.txt`, replaced atomically (write `phase.tmp`,
  flush, rename over), checked; unreadable is never treated as missing.
- File probes are three-way (missing / present / error); an error at any
  probe during undo stops it with "needs repair". Trash names are unique;
  `ERROR_PATH_NOT_FOUND` counts as missing.

## What goes away

The in-app install, undo, handshake wait and startup recovery code
(`install`, `undoInstall`, `recoverAtStartup`, `markConfirmed`, `yieldGpu`,
the `--after-update <process> <event>` handover), replaced by the helper.
No released version has the in-app installer (5.2.3 has no updater), so
nothing needs to stay compatible with it.

## Honest limits

- Power cut during step 4: a mix of old and new files, all present; the app
  starts (same Qt) and the helper undoes. If a future release changes the Qt
  version, a mixed state might not start; the helper by hand still recovers.
- The checksum still comes from the same release as the zip (T10).
- `tar.exe` still does the unpacking; the zip's list is checked before it
  runs and the result after.
- During the swap (a few seconds) no process manages the GPU (unlimited).

## Builder's calls

- The helper's command line is the only interface between versions:
  `install <process handle> [--show]` and `recover`; it finds the package
  itself (the one folder under `download\unpacked` holding the exe).
- Shared code: `updaterules` (pure) and a new `updatetxn` (Win32 transaction:
  journal, phase, swap, undo, tombstone) compiled into both programs; the
  app keeps network/prepare in `updater`.
- Timeouts: helper waits 30 s for the old app, 30 s for "up", 3 s grace.
- Test builds get pause/fault switches in the helper too (testhooks.h).
