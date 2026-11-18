# Check for updates: design

## Why

Update limit-nvpstate from inside the app instead of downloading and unzipping
a release by hand.

## Decided with the user

- Source: GitHub releases of **NicholasBly/limit-nvpstate** (a constant).
- **Button only** (Help → Check for updates…). No automatic checks, no
  network traffic unless asked.
- One click installs: download, verify, swap files, restart.
- After the second review (bac0835): keep one-click updates, but harden them
  (R2–R10 and the small items) rather than ship check-only.

## Behaviour

1. Help → Check for updates… asks the GitHub REST API for the latest
   (non-pre-release) release, in the background with a progress dialog.
   The tag must be a plain release version (1–4 parts of 1–9 digits).
2. Up to date → "You have the latest version (X)". Error → the reason plus a
   link to the releases page. All updater messages are plain text.
3. Newer → "Version Y is available (you have X)", release-notes link,
   [Update now] [Later]. A renamed exe is not offered Update now.
4. Update now (Cancel is honoured between every step, and checked again
   before anything is installed):
   - work folder: `limit-nvpstate-update\` inside the program folder, marked
     by `owner.txt`. Not in %TEMP%: nothing a normal program can write to
     sits between the checksum check and the install, unless the program
     folder itself is writable to it — and then so is the exe;
   - download exactly `limit-nvpstate-Y.zip` and `limit-nvpstate-Y.zip.sha256`
     over HTTPS; refuse unless the SHA-256 matches. A release without a
     checksum file (5.2.3 and older) gets "open the release page" instead;
   - unpack with Windows' built-in `tar.exe` (Windows 10 1803+). The package
     folder is the one folder holding `limit-nvpstate.exe`. Refuse a package
     with links, unsafe or device names, duplicates, listing errors, too many
     files or too many bytes, or without the exe, Qt5Core/Gui/Widgets and
     `platforms\qwindows.dll`;
   - plan the install (folders to create, files to add, files to replace;
     never an existing `config.json`; the exe last) and write it to
     `journal.txt`, flushed, before any program file moves;
   - replaced files go to `backup\`; any failure undoes the journal. If the
     undo itself fails, say so and keep the backups — never "nothing was
     changed" unless it is true. Then write `installed`;
   - stop detectors and release the GPU; if the release fails, undo and keep
     running;
   - start the new exe with `--after-update <process handle> <event handle>`
     (both inherited; only those two). The new instance initialises, sets the
     event, waits for the old process, then takes the single-instance mutex
     and deletes the work folder;
   - the old instance waits (without blocking) for the event: set → write
     `confirmed` and exit. The new process exits first, or 30 s pass → stop
     it, undo, re-apply the limit, keep running.
5. Exit, logoff or a fatal error while checking/downloading: nothing is
   installed, even if the download finishes inside an exit warning.
6. Every start (not one launched by the updater) looks at the work folder:
   no journal → old download, delete; `confirmed` → delete; `installed` but
   not confirmed → this is the new version: finish once it's up, undo if it
   can't start; otherwise an install was interrupted → undo and say so. It
   never deletes anything outside its own marked folder.

## Version

One number, compiled in: `APP_VERSION` from the MSBuild property
`AppVersion` (default in the project, overridden by `build.ps1 -Version`,
which the release workflow passes). The About box shows it. Versions compare
numerically by dotted parts ("5.2.10" > "5.2.9", leading "v" ignored).

## Release workflow

Validates the version input (`^[0-9]{1,9}(\.[0-9]{1,9}){0,3}$`, the same rule
the app applies to tags), builds with it, publishes `limit-nvpstate-<ver>.zip`
plus `limit-nvpstate-<ver>.zip.sha256` ("<hex>  <file name>", one line).

## Approaches considered

- **Open the download page only:** no file handling, but not what was asked
  ("update right from limit-nvpstate itself").
- **Qt Network + private QZipReader:** brings back Qt5Network.dll and OpenSSL
  DLLs (pruned today) and a private Qt API.
- **WinHTTP + BCrypt + tar.exe (chosen):** all built into Windows; no new
  DLLs to ship.
- **Versioned install folders + a separate launcher (review suggestion):**
  atomic switch, but a second program to ship and maintain and a changed
  install layout. The journal + handshake cover the same failures (crash
  mid-install, broken new version, failed undo) in place.

## Honest limits

- The checksum catches a broken or truncated download. It cannot catch a
  compromised GitHub account or release workflow, since both files come from
  the same release. Signing (a code-signing certificate, or a key kept off
  GitHub) would; not done.
- If the program folder is writable by normal programs (e.g. in Downloads),
  so is the work folder — and the exe. Keep it in a protected folder if that
  matters.
- If the new version passes the handshake and then crashes, or both copies
  die before the handshake and the new exe can't even start, nothing is left
  running to undo it; the backups stay in `limit-nvpstate-update\backup`.
- A stalled download is bounded by WinHTTP's per-step timeouts (15/30 s), not
  an overall deadline; Cancel takes effect after the current chunk.
- Needs `tar.exe` (Windows 10 1803 or newer); otherwise it says so and links
  the release page.

## Builder's calls

- Pure, unit-tested rules (versions, release tags, asset choice, checksum
  file, safe paths, journal format, package checks) in
  `updaterules.{h,cpp}`; Windows work in `updater.{h,cpp}`.
- Size caps: API reply 1 MiB, zip 200 MiB, unpacked 500 MiB / 1000 files.
- Test builds (the Wine rig) may point the API at a local server and use the
  pauses/fault switches in `testhooks.h`; release builds only accept
  `https://` URLs and compile the hooks to nothing.
